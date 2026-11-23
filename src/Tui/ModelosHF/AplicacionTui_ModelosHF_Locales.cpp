/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — Modelos HF: modelos ya descargados (listado y arranque).
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/TemaTui.h"
#include "Tui/Terminal/UtilTui.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Agente/LLM/HuggingFace/HfApi.h"
#include "Agente/LLM/HuggingFace/HfTypes.h"
#include "Agente/LLM/Local/LocalInference.h"

#include "Nucleo/CatalogoComandos.h"
#include "Nucleo/NucleoCLI.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"
#include "Util/Clipboard.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef FUNSHI_VERSION
#define FUNSHI_VERSION "0.0.0"
#endif

namespace funshi {

void AplicacionTui::cargarModelosLocalesHF() {
    modelosHFLocalesCargando_ = true;
    modelosHFError_.clear();

    std::thread([this]() {
        try {
            auto toLower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return s;
            };
            auto tieneExtensionModelo = [&](const std::filesystem::path& p) {
                const std::string ext = toLower(p.extension().string());
                return ext == ".gguf" || ext == ".bin";
            };

            auto config = funshi::agente::llm::LlmConfig::load();
            funshi::agente::llm::LocalInferenceManager manager(
                config.localInferenceDirEfectivo().string(),
                config.modelsDirEfectivo().string(),
                config.llamaServerPath.empty() ? std::string()
                                               : config.llamaServerPathEfectivo().string());

            std::vector<std::filesystem::path> dirs;
            auto agregarDir = [&](const std::filesystem::path& d) {
                if (d.empty()) return;
                std::error_code ec;
                auto norm = d;
                if (!std::filesystem::exists(norm, ec)) return;
                for (const auto& x : dirs) {
                    std::error_code ecEq;
                    if (std::filesystem::equivalent(x, norm, ecEq) && !ecEq) return;
                }
                dirs.push_back(norm);
            };

            agregarDir(manager.modelsDir());
            agregarDir(config.modelsDirEfectivo());
            agregarDir(config.localInferenceDirEfectivo() / "models");
            agregarDir(std::filesystem::current_path() / ".agentes" / "local-inference" / "models");
            const char* home = std::getenv("HOME");
#ifdef _WIN32
            if (!home) home = std::getenv("USERPROFILE");
#endif
            if (home) {
                agregarDir(std::filesystem::path(home) / ".agentes" / "local-inference" / "models");
            }

            std::vector<std::filesystem::path> encontrados;
            for (const auto& dir : dirs) {
                std::error_code ec;
                if (!std::filesystem::is_directory(dir, ec)) continue;
                for (const auto& ent : std::filesystem::directory_iterator(dir, ec)) {
                    if (ec) break;
                    std::error_code ec2;
                    if (!ent.is_regular_file(ec2) || ec2) continue;
                    if (!tieneExtensionModelo(ent.path())) continue;
                    bool yaExiste = false;
                    for (const auto& e : encontrados) {
                        std::error_code ecEq;
                        if (std::filesystem::equivalent(e, ent.path(), ecEq) && !ecEq) {
                            yaExiste = true;
                            break;
                        }
                    }
                    if (!yaExiste) encontrados.push_back(ent.path());
                }
            }

            // Completos primero (más recientes arriba); los truncados al final.
            struct Entrada {
                std::filesystem::path ruta;
                bool completo;
            };
            std::vector<Entrada> entradas;
            entradas.reserve(encontrados.size());
            for (auto& p : encontrados) {
                const bool completo = funshi::agente::llm::inspeccionarArchivoModelo(p).completo;
                entradas.push_back({std::move(p), completo});
            }
            std::sort(entradas.begin(), entradas.end(), [](const Entrada& a, const Entrada& b) {
                if (a.completo != b.completo) return a.completo;
                std::error_code ea, eb;
                auto ta = std::filesystem::last_write_time(a.ruta, ea);
                auto tb = std::filesystem::last_write_time(b.ruta, eb);
                if (!ea && !eb && ta != tb) return ta > tb;
                return a.ruta.filename().string() < b.ruta.filename().string();
            });

            // Activo: el config solo guarda el nombre del archivo; se marca la
            // copia completa con ese nombre, prefiriendo la de modelsDir.
            int activo = -1;
            const auto modelsDir = config.modelsDirEfectivo();
            for (int i = 0; i < static_cast<int>(entradas.size()); ++i) {
                const auto& e = entradas[i];
                if (!e.completo || e.ruta.filename().string() != config.modelName) continue;
                std::error_code ecEq;
                const bool enModelsDir = std::filesystem::equivalent(e.ruta.parent_path(), modelsDir, ecEq) && !ecEq;
                if (activo < 0 || enModelsDir) activo = i;
                if (enModelsDir) break;
            }

            std::vector<std::filesystem::path> rutas;
            std::vector<bool> completos;
            for (auto& e : entradas) {
                rutas.push_back(std::move(e.ruta));
                completos.push_back(e.completo);
            }

            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFLocales_ = std::move(rutas);
            modelosHFLocalCompleto_ = std::move(completos);
            modelosHFLocalActivo_ = activo;
            modelosHFLocalesCargando_ = false;
            if (modelosHFLocalSeleccion_ >= static_cast<int>(modelosHFLocales_.size())) {
                modelosHFLocalSeleccion_ = std::max(0, static_cast<int>(modelosHFLocales_.size()) - 1);
            }
            modelosHFLocalDesplazamiento_ = 0;
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFLocalesCargando_ = false;
            modelosHFError_ = "Error listando modelos locales: " + std::string(e.what());
        } catch (...) {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFLocalesCargando_ = false;
            modelosHFError_ = "Error inesperado listando modelos locales";
        }
        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    }).detach();
}

void AplicacionTui::iniciarConfiguracionModeloLocal(const std::filesystem::path& rutaModelo) {
    modelosHFEstadoRetorno_ = ModeloHFEstado::LISTA_LOCALES;
    modeloHFSeleccionadoInfo_.id = rutaModelo.filename().string();
    modelosHFEstado_ = ModeloHFEstado::CONFIGURANDO;
    mensaje_ = "Configurando " + rutaModelo.filename().string() + "...";
    mensajeEsError_ = false;

    {
        std::lock_guard<std::mutex> lock(modelosHFMutex_);
        modelosHFStatus_ = "Preparando modelo local: " + rutaModelo.filename().string();
        modelosHFProgreso_ = 0.0;
        modelosHFError_.clear();
    }
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());

    std::thread([this, rutaModelo]() {
        try {
            auto progressCb = [this](double p, const std::string& status) {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFProgreso_ = p;
                modelosHFStatus_ = status;
                if (pantalla_) pantalla_->PostEvent(ftxui::Event());
            };

            funshi::agente::llm::LlmConfig config = funshi::agente::llm::LlmConfig::load();
            funshi::agente::llm::LocalInferenceManager manager(
                config.localInferenceDirEfectivo().string(),
                config.modelsDirEfectivo().string(),
                config.llamaServerPath.empty() ? std::string()
                                               : config.llamaServerPathEfectivo().string());

            std::error_code ec;
            if (!std::filesystem::exists(rutaModelo, ec)) {
                throw std::runtime_error("El archivo del modelo no existe: " + rutaModelo.string());
            }

            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Preparando llama-server...";
                modelosHFProgreso_ = -1.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            if (!manager.ensureLlamaServer(progressCb)) {
                throw std::runtime_error(manager.lastError());
            }

            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Iniciando servidor...";
                modelosHFProgreso_ = 0.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            if (!manager.startServer(rutaModelo, 11434, config.gpuLayers,
                                     config.contextSize > 0 ? config.contextSize
                                                            : funshi::agente::llm::kContextoPorDefecto,
                                     progressCb)) {
                throw std::runtime_error("Error iniciando servidor: " + manager.lastError());
            }

            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Configurando Silvy...";
                modelosHFProgreso_ = 0.9;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            config.mode = funshi::agente::llm::LlmMode::REMOTE;
            config.remoteBaseUrl = manager.getServerInfo().baseUrl;
            config.apiKey = "local";
            config.modelName = rutaModelo.filename().string();
            if (config.contextSize <= 0) config.contextSize = funshi::agente::llm::kContextoPorDefecto;
            if (config.maxTokens <= 0) config.maxTokens = 4096;

            // Si la plantilla del modelo no admite `tools` nativas, llama-server
            // las ignora: se usa el modo prompt (bloques ```tool_call```).
            const auto caps = manager.consultarCapacidades();
            const bool nativas = caps.consultado ? caps.herramientasNativas : true;
            if (!silvy_.configurarLlm(config, nativas)) {
                throw std::runtime_error("Error configurando Silvy con el modelo local");
            }

            config.save();
            manager.detachServer();

            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "¡Listo! Modelo " + rutaModelo.filename().string() +
                                   " activo en " + config.remoteBaseUrl;
                modelosHFProgreso_ = 1.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFEstado_ = ModeloHFEstado::ERROR_CONFIG;
            modelosHFError_ = "Error: " + std::string(e.what());
            modelosHFStatus_ = "Error: " + std::string(e.what());
            modelosHFProgreso_ = 0.0;
        }

        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    }).detach();
}


}  // namespace funshi
