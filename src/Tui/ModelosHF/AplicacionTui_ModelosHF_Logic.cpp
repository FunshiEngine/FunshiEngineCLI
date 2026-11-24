/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — Modelos HF: búsqueda, variantes y descarga+configuración.
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

namespace {

// Una variante está descargada si su archivo está en modelsDir con el tamaño
// esperado (si el tamaño no se conoce, basta con que exista).
std::vector<bool> calcularDescargadas(const std::vector<funshi::agente::llm::ModelVariant>& variantes,
                                      const std::filesystem::path& modelsDir) {
    std::vector<bool> marcas;
    marcas.reserve(variantes.size());
    for (const auto& v : variantes) {
        std::error_code ec;
        const auto ruta = modelsDir / v.filename;
        const auto tam = std::filesystem::file_size(ruta, ec);
        marcas.push_back(!ec && tam > 0 && (v.sizeBytes == 0 || tam == v.sizeBytes));
    }
    return marcas;
}

}  // namespace


void AplicacionTui::iniciarAutoConfiguracionModelo(const funshi::agente::llm::ModelVariant& variant) {
    modelosHFEstadoRetorno_ = ModeloHFEstado::LISTA_VARIANTES;
    modelosHFEstado_ = ModeloHFEstado::CONFIGURANDO;
    mensaje_ = "Configurando " + variant.filename + "...";
    mensajeEsError_ = false;

    {
        std::lock_guard<std::mutex> lock(modelosHFMutex_);
        modelosHFStatus_ = "Iniciando configuración de " + variant.filename + "...";
        modelosHFProgreso_ = 0.0;
    }
    // Forzar redibujado inmediato para que el usuario vea que se inició
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());

    std::thread([this, variant]() {
        try {
            // 1. Descargar modelo
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

            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Descargando modelo...";
                modelosHFProgreso_ = 0.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            if (!manager.downloadModel(variant, progressCb)) {
                throw std::runtime_error(manager.lastError());
            }

            // 2. Asegurar el binario oficial de llama-server
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Preparando llama-server...";
                modelosHFProgreso_ = -1.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            if (!manager.ensureLlamaServer(progressCb)) {
                throw std::runtime_error(manager.lastError());
            }

            // 3. Iniciar servidor
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Iniciando servidor...";
                modelosHFProgreso_ = 0.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            std::filesystem::path modelPath = manager.modelsDir() / variant.filename;
            if (!manager.startServer(modelPath, 11434, config.gpuLayers,
                                     config.contextSize > 0 ? config.contextSize
                                                            : funshi::agente::llm::kContextoPorDefecto,
                                     progressCb)) {
                throw std::runtime_error("Error iniciando servidor: " + manager.lastError());
            }

            // 4. Configurar Silvy
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Configurando Silvy...";
                modelosHFProgreso_ = 0.9;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            config.mode = funshi::agente::llm::LlmMode::REMOTE;
            config.remoteBaseUrl = manager.getServerInfo().baseUrl;
            config.apiKey = "local";  // localhost no requiere API key real
            config.modelName = variant.filename;  // nombre del archivo GGUF
            if (config.contextSize <= 0) config.contextSize = funshi::agente::llm::kContextoPorDefecto;
            if (config.maxTokens <= 0) config.maxTokens = 4096;

            // Si la plantilla del modelo no admite `tools` nativas, llama-server
            // las ignora: se usa el modo prompt (bloques ```tool_call```).
            const auto caps = manager.consultarCapacidades();
            const bool nativas = caps.consultado ? caps.herramientasNativas : true;
            if (!silvy_.configurarLlm(config, nativas)) {
                throw std::runtime_error("Error configurando Silvy con el modelo local");
            }

            // Guardar config para persistencia (conservando rutas configuradas)
            config.save();

            // El servidor queda corriendo para que Silvy pueda usarlo.
            manager.detachServer();

            // Éxito: mantener estado CONFIGURANDO con progreso 1.0 y mensaje de éxito
            // El usuario debe pulsar Escape para volver a la lista de variantes
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "¡Listo! Modelo " + variant.filename + " activo en " + config.remoteBaseUrl;
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

void AplicacionTui::reconocerFinConfiguracionModelo() {
    ModeloHFEstado destino;
    {
        std::lock_guard<std::mutex> lock(modelosHFMutex_);
        if (modelosHFEstado_ != ModeloHFEstado::CONFIGURANDO &&
            modelosHFEstado_ != ModeloHFEstado::ERROR_CONFIG) {
            return;
        }
        modelosHFEstado_ = modelosHFEstadoRetorno_;
        modelosHFError_.clear();
        modelosHFStatus_.clear();
        modelosHFProgreso_ = 0.0;
        destino = modelosHFEstado_;
    }
    // Refrescar marcas: el modelo pudo quedar descargado o pasar a ser el activo.
    if (destino == ModeloHFEstado::LISTA_LOCALES) {
        cargarModelosLocalesHF();
    } else if (destino == ModeloHFEstado::LISTA_VARIANTES) {
        marcarVariantesDescargadas();
    }
}

void AplicacionTui::marcarVariantesDescargadas() {
    const auto config = funshi::agente::llm::LlmConfig::load();
    std::lock_guard<std::mutex> lock(modelosHFMutex_);
    modelosHFVariantDescargada_ = calcularDescargadas(modelosHFVariants_, config.modelsDirEfectivo());
}

void AplicacionTui::abrirModelosLocalesHF() {
    modelosHFEstado_ = ModeloHFEstado::LISTA_LOCALES;
    modelosHFLocalSeleccion_ = 0;
    modelosHFLocalDesplazamiento_ = 0;
    modelosHFError_.clear();
    focoEnCajaBusquedaModelosHF_ = false;
    mensaje_ = "Modelos descargados: Enter usa el modelo · Tab busca en Hugging Face";
    mensajeEsError_ = false;
    cargarModelosLocalesHF();
}

void AplicacionTui::abrirBusquedaModelosHF() {
    modelosHFEstado_ = ModeloHFEstado::LISTA_MODELOS;
    modelosHFVariants_.clear();
    modelosHFVariantDescargada_.clear();
    modelosHFVariantSeleccion_ = 0;
    modelosHFVariantsCargando_ = false;
    modelosHFError_.clear();
    focoEnCajaBusquedaModelosHF_ = true;
    mensaje_ = "Buscar en Hugging Face: escribe y pulsa Enter · Tab vuelve a descargados";
    mensajeEsError_ = false;
}

void AplicacionTui::ejecutarBusquedaModelosHF() {
    const std::string query = textoModelosHF_;
    if (query.empty()) return;

    modelosHFBuscando_ = true;
    modelosHFError_.clear();
    modelosHFItems_.clear();
    modelosHFSeleccion_ = 0;
    modelosHFDesplazamiento_ = 0;

    // Búsqueda asíncrona usando thread
    std::thread([this, query]() {
        try {
            auto config = funshi::agente::llm::LlmConfig::load();
            funshi::agente::llm::HfApi api(config.hfCacheDirEfectivo().string());
            if (!config.hfToken.empty()) api.setToken(config.hfToken);
            funshi::agente::llm::SearchFilters filters;
            filters.query = query;
            filters.limit = 20;
            filters.sort = "downloads";

            auto result = api.search(filters, [this](double p) {
                (void)p;
                // Progreso de búsqueda - actualizar UI si necesario
            });

            // Actualizar en el hilo principal
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFItems_ = std::move(result.models);
            modelosHFBuscando_ = false;
            if (modelosHFItems_.empty()) {
                modelosHFError_ = "No se encontraron modelos";
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFBuscando_ = false;
            modelosHFError_ = std::string(e.what()) + "  (r reintenta · l modelos locales)";
        } catch (...) {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFBuscando_ = false;
            modelosHFError_ = "Error inesperado buscando modelos";
        }

        // Forzar redibujado
        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    }).detach();
}

void AplicacionTui::cargarVariantesModeloSeleccionado(bool autoSeleccionarPrimera) {
    modelosHFVariantsCargando_ = true;
    modelosHFError_.clear();

    std::thread([this, autoSeleccionarPrimera]() {
        try {
            auto config = funshi::agente::llm::LlmConfig::load();
            funshi::agente::llm::HfApi api(config.hfCacheDirEfectivo().string());
            if (!config.hfToken.empty()) api.setToken(config.hfToken);
            auto variants = api.getVariants(modeloHFSeleccionadoInfo_.id, nullptr, true);
            auto descargadas = calcularDescargadas(variants, config.modelsDirEfectivo());

            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFVariants_ = std::move(variants);
            modelosHFVariantDescargada_ = std::move(descargadas);
            modelosHFVariantsCargando_ = false;

            // Si hay variantes y se pidió auto-seleccionar la primera
            if (autoSeleccionarPrimera && !modelosHFVariants_.empty()) {
                modelosHFVariantSeleccion_ = 0;
                // Lanzar auto-configuración directamente en este hilo (ya tenemos el lock)
                // pero liberamos el lock antes de llamar a iniciarAutoConfiguracionModelo
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFVariantsCargando_ = false;
            modelosHFError_ = "Error cargando variantes: " + std::string(e.what());
        } catch (...) {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFVariantsCargando_ = false;
            modelosHFError_ = "Error inesperado cargando variantes";
        }

        if (pantalla_) {
            // Si se pidió auto-seleccionar y hay variantes, postear Return para que el handler lo procese
            if (autoSeleccionarPrimera && !modelosHFVariants_.empty()) {
                pantalla_->PostEvent(ftxui::Event::Return);
            } else {
                pantalla_->PostEvent(ftxui::Event());
            }
        }
    }).detach();
}

}  // namespace funshi
