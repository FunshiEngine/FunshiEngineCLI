/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — módulo modelos
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
            modelosHFVariantDescargada_.clear();
            modelosHFVariantSeleccion_ = 0;
            modelosHFVariantsCargando_ = false;
            focoEnCajaBusquedaModelosHF_ = true;
            return true;
        }
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
            // «Descargados» es la vista inicial: Esc sale del panel.
            desenfocarModelosHF();
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
    // Tab alterna entre «Descargados» y «Buscar en Hugging Face».
    if (evento == ftxui::Event::Tab || evento == ftxui::Event::TabReverse) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
            abrirBusquedaModelosHF();
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS ||
                   modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            abrirModelosLocalesHF();
        }
        return true;
    }
    if (evento == ftxui::Event::ArrowUp) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            if (modelosHFVariantSeleccion_ > 0) modelosHFVariantSeleccion_--;
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
            if (modelosHFLocalSeleccion_ > 0) modelosHFLocalSeleccion_--;
        } else {
            if (!modelosHFItems_.empty()) focoEnCajaBusquedaModelosHF_ = false;
            if (modelosHFSeleccion_ > 0) modelosHFSeleccion_--;
        }
        return true;
    }
    if (evento == ftxui::Event::ArrowDown) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            if (modelosHFVariantSeleccion_ < static_cast<int>(modelosHFVariants_.size()) - 1) modelosHFVariantSeleccion_++;
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
            if (modelosHFLocalSeleccion_ < static_cast<int>(modelosHFLocales_.size()) - 1) modelosHFLocalSeleccion_++;
        } else {
            if (!modelosHFItems_.empty()) focoEnCajaBusquedaModelosHF_ = false;
            if (modelosHFSeleccion_ < static_cast<int>(modelosHFItems_.size()) - 1) modelosHFSeleccion_++;
        }
        return true;
    }
    if (evento == ftxui::Event::PageUp) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            modelosHFVariantSeleccion_ = std::max(0, modelosHFVariantSeleccion_ - filasVisibles());
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
            modelosHFLocalSeleccion_ = std::max(0, modelosHFLocalSeleccion_ - filasVisibles());
        } else {
            if (!modelosHFItems_.empty()) focoEnCajaBusquedaModelosHF_ = false;
            modelosHFSeleccion_ = std::max(0, modelosHFSeleccion_ - filasVisibles());
            if (modelosHFSeleccion_ < modelosHFDesplazamiento_) modelosHFDesplazamiento_ = modelosHFSeleccion_;
        }
        return true;
    }
    if (evento == ftxui::Event::PageDown) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            modelosHFVariantSeleccion_ += filasVisibles();
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
            modelosHFLocalSeleccion_ += filasVisibles();
        } else {
            if (!modelosHFItems_.empty()) focoEnCajaBusquedaModelosHF_ = false;
            if (!modelosHFItems_.empty()) {
                modelosHFSeleccion_ = std::min(static_cast<int>(modelosHFItems_.size()) - 1,
                                               modelosHFSeleccion_ + filasVisibles());
            }
        }
        return true;
    }
    if (evento == ftxui::Event::Return) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
            // Si hay foco en la caja de búsqueda, ejecutar búsqueda
            // Si no hay foco (navegando con flechas), seleccionar modelo de la lista
            if (focoEnCajaBusquedaModelosHF_ && !textoModelosHF_.empty()) {
                ejecutarBusquedaModelosHF();
                focoEnCajaBusquedaModelosHF_ = false;
            } else if (modelosHFSeleccion_ >= 0 && modelosHFSeleccion_ < static_cast<int>(modelosHFItems_.size())) {
                modeloHFSeleccionadoInfo_ = modelosHFItems_[modelosHFSeleccion_];
                modelosHFVariants_.clear();
                modelosHFVariantSeleccion_ = 0;
                modelosHFError_.clear();
                modelosHFEstado_ = ModeloHFEstado::LISTA_VARIANTES;
                cargarVariantesModeloSeleccionado(false);
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
        } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
            if (modelosHFLocalesCargando_) return true;
            if (modelosHFLocalSeleccion_ >= 0 &&
                modelosHFLocalSeleccion_ < static_cast<int>(modelosHFLocales_.size())) {
                const bool completo =
                    modelosHFLocalSeleccion_ >= static_cast<int>(modelosHFLocalCompleto_.size()) ||
                    modelosHFLocalCompleto_[modelosHFLocalSeleccion_];
                if (!completo) {
                    mensaje_ = "Ese archivo está incompleto (descarga cortada): bórralo o vuelve a "
                               "descargarlo desde «Buscar en Hugging Face»";
                    mensajeEsError_ = true;
                    return true;
                }
                iniciarConfiguracionModeloLocal(modelosHFLocales_[modelosHFLocalSeleccion_]);
            }
            return true;
        } else {
            // En modo configurando: no hacer nada con Enter
            return true;
        }
    }
    // Tecla 'r' para reintentar carga
    if (evento == ftxui::Event::Character('r') || evento == ftxui::Event::Character('R')) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES && !modelosHFVariantsCargando_) {
            if (!modelosHFError_.empty() || modelosHFVariants_.empty()) {
                cargarVariantesModeloSeleccionado(false);
                return true;
            }
        }
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES && !modelosHFLocalesCargando_) {
            cargarModelosLocalesHF();
            return true;
        }
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS && !modelosHFBuscando_ && !focoEnCajaBusquedaModelosHF_) {
            if (!modelosHFError_.empty() || modelosHFItems_.empty()) {
                ejecutarBusquedaModelosHF();
                return true;
            }
        }
    }
    // Tecla 'l' para listar modelos ya descargados/locales
    if (evento == ftxui::Event::Character('l') || evento == ftxui::Event::Character('L')) {
        if ((modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS && !focoEnCajaBusquedaModelosHF_) ||
            modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
            abrirModelosLocalesHF();
            return true;
        }
    }

    // Tecla 'c' para limpiar cache HF completo
    if (evento == ftxui::Event::Character('c') || evento == ftxui::Event::Character('C')) {
        if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES ||
            (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS && !focoEnCajaBusquedaModelosHF_)) {
            auto config = funshi::agente::llm::LlmConfig::load();
            funshi::agente::llm::HfApi api(config.hfCacheDirEfectivo().string());
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
    if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
        focoEnCajaBusquedaModelosHF_ = false;
        mensaje_ = "Modelos descargados: Enter usa el modelo · Tab busca en Hugging Face";
    } else {
        focoEnCajaBusquedaModelosHF_ = (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS);
        mensaje_ = "Buscar en Hugging Face: escribe y pulsa Enter · Tab vuelve a descargados";
    }
    mensajeEsError_ = false;
}

void AplicacionTui::desenfocarModelosHF() {
    focoEnModelosHF_ = false;
    focoEnCajaBusquedaModelosHF_ = false;
}


}  // namespace funshi
