/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — módulo modelos
#include "Tui/AplicacionTui.h"

#include "Tui/TemaTui.h"
#include "Tui/UtilTui.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Agente/LLM/HfApi.h"
#include "Agente/LLM/HfTypes.h"
#include "Agente/LLM/LocalInference.h"

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


void AplicacionTui::iniciarAutoConfiguracionModelo(const funshi::agente::llm::ModelVariant& variant) {
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

            funshi::agente::llm::LocalInferenceManager manager;
            
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Descargando modelo...";
                modelosHFProgreso_ = 0.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());
            
            if (!manager.downloadModel(variant, progressCb)) {
                throw std::runtime_error("Error descargando modelo: " + manager.lastError());
            }

            // 2. Asegurar llama.cpp compilado
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Compilando llama.cpp...";
                modelosHFProgreso_ = 0.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());
            
            if (!manager.ensureLlamaCppBuilt(progressCb)) {
                throw std::runtime_error("Error compilando llama.cpp: " + manager.lastError());
            }

            // 3. Iniciar servidor
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Iniciando servidor...";
                modelosHFProgreso_ = 0.0;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            std::filesystem::path modelPath = manager.modelsDir() / variant.filename;
            if (!manager.startServer(modelPath, 11434, -1, 8192, progressCb)) {
                throw std::runtime_error("Error iniciando servidor: " + manager.lastError());
            }

            // 4. Configurar Silvy
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "Configurando Silvy...";
                modelosHFProgreso_ = 0.9;
            }
            if (pantalla_) pantalla_->PostEvent(ftxui::Event());

            funshi::agente::llm::LlmConfig config;
            config.mode = funshi::agente::llm::LlmMode::REMOTE;
            config.remoteBaseUrl = manager.getServerInfo().baseUrl;
            config.apiKey = "local";  // localhost no requiere API key real
            config.modelName = variant.filename;  // nombre del archivo GGUF
            config.temperature = 0.7;
            config.maxTokens = 4096;
            config.chatTimeoutMs = 30000;
            config.toolTimeoutMs = 30000;
            config.contextSize = 8192;
            config.gpuLayers = -1;

            if (!silvy_.configurarLlm(config)) {
                throw std::runtime_error("Error configurando Silvy con el modelo local");
            }

            // Guardar config para persistencia
            config.save();

            // Éxito: mantener estado CONFIGURANDO con progreso 1.0 y mensaje de éxito
            // El usuario debe pulsar Escape para volver a la lista de variantes
            {
                std::lock_guard<std::mutex> lock(modelosHFMutex_);
                modelosHFStatus_ = "¡Listo! Modelo " + variant.filename + " activo en " + manager.getServerInfo().baseUrl;
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
    std::lock_guard<std::mutex> lock(modelosHFMutex_);
    if (modelosHFEstado_ == ModeloHFEstado::CONFIGURANDO || modelosHFEstado_ == ModeloHFEstado::ERROR_CONFIG) {
        modelosHFEstado_ = ModeloHFEstado::LISTA_VARIANTES;
        modelosHFError_.clear();
        modelosHFStatus_.clear();
        modelosHFProgreso_ = 0.0;
    }
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
            funshi::agente::llm::HfApi api;
            funshi::agente::llm::SearchFilters filters;
            filters.query = query;
            filters.limit = 20;
            filters.sort = "downloads";
            
            auto result = api.search(filters, [this](double p) {
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
            modelosHFError_ = "Error: " + std::string(e.what());
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
            funshi::agente::llm::HfApi api;
            auto variants = api.getVariants(modeloHFSeleccionadoInfo_.id, nullptr, true);
            
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            modelosHFVariants_ = std::move(variants);
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
