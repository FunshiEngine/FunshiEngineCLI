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


bool AplicacionTui::eventoEnModelosHF(ftxui::Event evento) {
    if (evento == ftxui::Event::CtrlC || evento == ftxui::Event::Character('\x03')) {
        pantalla_->Exit();
        return true;
    }
    if (evento == ftxui::Event::Escape) {
        if (modelosHFEstado_ == ModeloHFEstado::CONFIGURANDO || modelosHFEstado_ == ModeloHFEstado::ERROR_CONFIG) {
            // Volver a lista de variantes tras éxito o error
            reconocerFinConfiguracionModelo();
            return true;
        }
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            // Volver a lista de modelos, con foco en la caja de búsqueda
            modelosHFEstado_ = ModeloHFEstado::LISTA_MODELOS;
            modelosHFVariants_.clear();
            modelosHFVariantSeleccion_ = 0;
            modelosHFVariantsCargando_ = false;
            focoEnCajaBusquedaModelosHF_ = true;
            return true;
        }
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
            // Si el foco está en la lista (no en la caja), devolver foco a la caja
            if (!focoEnCajaBusquedaModelosHF_) {
                focoEnCajaBusquedaModelosHF_ = true;
                return true;
            }
            // Si ya está en la caja, salir de ModelosHF
            desenfocarModelosHF();
            return true;
        }
        desenfocarModelosHF();
        return true;
    }
    if (evento == ftxui::Event::ArrowUp) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            if (modelosHFVariantSeleccion_ > 0) modelosHFVariantSeleccion_--;
        } else {
            if (modelosHFSeleccion_ > 0) modelosHFSeleccion_--;
        }
        return true;
    }
    if (evento == ftxui::Event::ArrowDown) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            if (modelosHFVariantSeleccion_ < static_cast<int>(modelosHFVariants_.size()) - 1) modelosHFVariantSeleccion_++;
        } else {
            if (modelosHFSeleccion_ < static_cast<int>(modelosHFItems_.size()) - 1) modelosHFSeleccion_++;
        }
        return true;
    }
    if (evento == ftxui::Event::PageUp) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            modelosHFVariantSeleccion_ = std::max(0, modelosHFVariantSeleccion_ - filasVisibles());
        } else {
            modelosHFDesplazamiento_ = std::max(0, modelosHFDesplazamiento_ - filasVisibles());
        }
        return true;
    }
    if (evento == ftxui::Event::PageDown) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            modelosHFVariantSeleccion_ += filasVisibles();
        } else {
            modelosHFDesplazamiento_ += filasVisibles();
        }
        return true;
    }
    if (evento == ftxui::Event::Return) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
            // Si hay foco en la caja de búsqueda, ejecutar búsqueda
            // Si no hay foco (navegando con flechas), seleccionar modelo de la lista
            if (focoEnCajaBusquedaModelosHF_ && !textoModelosHF_.empty()) {
                ejecutarBusquedaModelosHF();
                // Después de buscar, quitar foco de la caja para que
                // el siguiente Enter seleccione el modelo de la lista
                // en lugar de buscar de nuevo. El foco en la lista se
                // mantiene (focoEnModelosHF_ = true) para navegar con
                // flechas y dar Enter sobre el modelo deseado.
                focoEnCajaBusquedaModelosHF_ = false;
            } else if (modelosHFSeleccion_ >= 0 && modelosHFSeleccion_ < static_cast<int>(modelosHFItems_.size())) {
                modeloHFSeleccionadoInfo_ = modelosHFItems_[modelosHFSeleccion_];
                modelosHFVariants_.clear();
                modelosHFVariantSeleccion_ = 0;
                modelosHFVariantsCargando_ = true;
                modelosHFError_.clear();
                modelosHFEstado_ = ModeloHFEstado::LISTA_VARIANTES;

                // Cargar variantes reales con tamaño y SHA256 vía ?blobs=true
                // (HfApi::getVariants ya usa ?blobs=true y devuelve size + lfs.sha256).
                // forceRefresh=true para ignorar cache y obtener datos frescos.
                std::thread([this]() {
                    try {
                        funshi::agente::llm::HfApi api;
                        auto variants = api.getVariants(modeloHFSeleccionadoInfo_.id, nullptr, true);
                        std::lock_guard<std::mutex> lock(modelosHFMutex_);
                        modelosHFVariants_ = std::move(variants);
                        modelosHFVariantsCargando_ = false;
                    } catch (const std::exception& e) {
                        std::lock_guard<std::mutex> lock(modelosHFMutex_);
                        modelosHFVariantsCargando_ = false;
                        modelosHFError_ = "Error cargando variantes: " + std::string(e.what());
                    }
                    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
                }).detach();
            }
            return true;
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            // Seleccionar variante -> iniciar auto-configuración completa
            if (modelosHFVariantsCargando_) {
                // Las variantes aún se están cargando: ignorar Enter
                return true;
            }
            if (!modelosHFVariants_.empty()) {
                // Hay variantes cargadas: usar la seleccionada
                modelosHFStatus_ = "Seleccionando: " + modelosHFVariants_[modelosHFVariantSeleccion_].filename;
                iniciarAutoConfiguracionModelo(modelosHFVariants_[modelosHFVariantSeleccion_]);
            } else {
                // No hay variantes cargadas: cargar automáticamente y usar la primera como base
                cargarVariantesModeloSeleccionado(true);
            }
            return true;
        } else {
            // En modo configurando: no hacer nada con Enter
            return true;
        }
    }
    // Tecla 'r' para reintentar carga de variantes cuando hay error
    if (evento == ftxui::Event::Character('r') || evento == ftxui::Event::Character('R')) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES && !modelosHFVariantsCargando_) {
            if (!modelosHFError_.empty() || modelosHFVariants_.empty()) {
                cargarVariantesModeloSeleccionado(false);
                return true;
            }
        }
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS && !modelosHFBuscando_) {
            if (!modelosHFError_.empty() || modelosHFItems_.empty()) {
                ejecutarBusquedaModelosHF();
                return true;
            }
        }
    }
    // Tecla 'c' para limpiar cache HF completo
    if (evento == ftxui::Event::Character('c') || evento == ftxui::Event::Character('C')) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS || modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            funshi::agente::llm::HfApi api;
            api.cleanCache();
            modelosHFError_ = "Cache HF limpiado";
            if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
                ejecutarBusquedaModelosHF();
            } else {
                cargarVariantesModeloSeleccionado(false);
            }
            return true;
        }
    }
    return entradaModelosHF_->OnEvent(evento);
}

void AplicacionTui::enfocarModelosHF() {
    focoEnModelosHF_ = true;
    focoEnCajaBusquedaModelosHF_ = true;
    mensaje_ = "Modelos HF: escribe término y pulsa Enter (Esc para volver)";
    mensajeEsError_ = false;
}

void AplicacionTui::desenfocarModelosHF() {
    focoEnModelosHF_ = false;
    focoEnCajaBusquedaModelosHF_ = false;
}


}  // namespace funshi
