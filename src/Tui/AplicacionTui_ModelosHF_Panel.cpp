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


ftxui::Element AplicacionTui::panelModelosHF() {
    std::vector<ftxui::Element> filas;
    filas.push_back(ftxui::hbox({
        ftxui::text(" Modelos HF  (busca modelos en Hugging Face)") | ftxui::bold,
        ftxui::filler(),
    }));
    filas.push_back(ftxui::separator());

    // Caja de búsqueda (solo en modo LISTA_MODELOS)
    if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
        ftxui::Element entradaBuscar = entradaModelosHF_->Render();
        filas.push_back(ftxui::hbox({
            ftxui::text(focoEnModelosHF_ ? " > " : " / ") | ftxui::color(kAcento),
            std::move(entradaBuscar) | ftxui::flex,
        }) | ftxui::bgcolor(kFondoBarra));

        filas.push_back(ftxui::text(" / busca · Enter selecciona modelo · Esc vuelve ") |
                        ftxui::color(kTextoTenue) | ftxui::bgcolor(kFondoBarra));
    } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
        // Mostrar info del modelo seleccionado (feedback claro de selección)
        filas.push_back(ftxui::text("  Modelo seleccionado:") | ftxui::color(kTextoTenue));
        filas.push_back(ftxui::text("  " + modeloHFSeleccionadoInfo_.id) |
                        ftxui::bold | ftxui::color(kExito));
        if (modelosHFVariantsCargando_) {
            filas.push_back(ftxui::text("  Variantes: cargando...") | ftxui::color(kAcento));
        } else {
            filas.push_back(ftxui::text("  Variantes disponibles: " +
                                        std::to_string(modelosHFVariants_.size())) |
                            ftxui::color(kAcento));
        }
        filas.push_back(ftxui::separator());
        filas.push_back(ftxui::text("  Variantes (Enter para configurar, Esc para volver):") |
                        ftxui::color(kTextoTenue));
        filas.push_back(ftxui::separator());
    } else if (modelosHFEstado_ == ModeloHFEstado::CONFIGURANDO) {
        // Feedback de progreso: paso actual + barra
        {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            filas.push_back(ftxui::text("  Instalando modelo:") | ftxui::color(kTextoTenue));
            filas.push_back(ftxui::text("  " + modeloHFSeleccionadoInfo_.id) |
                            ftxui::bold | ftxui::color(kExito));
            filas.push_back(ftxui::separator());
            if (!modelosHFStatus_.empty()) {
                filas.push_back(ftxui::text("  Paso: " + modelosHFStatus_) |
                                ftxui::color(kAcento) | ftxui::bold);
            }
            filas.push_back(ftxui::separator());
            // Barra de progreso: indeterminada (-1.0) = animación, 0-1 = barra normal
            std::string bar = "[";
            int barWidth = 40;
            if (modelosHFProgreso_ < 0.0) {
                // Progreso indeterminado: animación de "carga"
                const char* frames = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
                int frameIdx = modelosHFFrame_ % 10;
                bar += frames[frameIdx];
                for (int i = 1; i < barWidth; ++i) bar += " ";
                bar += "] Descargando...";
            } else {
                int filled = static_cast<int>(modelosHFProgreso_ * barWidth);
                for (int i = 0; i < barWidth; ++i) {
                    bar += (i < filled) ? "=" : " ";
                }
                bar += "] " + std::to_string(static_cast<int>(modelosHFProgreso_ * 100)) + "%";
            }
            filas.push_back(ftxui::text("  " + bar) | ftxui::color(kAcento));
            // Pista de tecla
            filas.push_back(ftxui::text("  (Esc para volver a variantes)") | ftxui::color(kTextoTenue));
        }
    } else if (modelosHFEstado_ == ModeloHFEstado::ERROR_CONFIG) {
        // Pantalla de error tras fallo de configuración
        {
            std::lock_guard<std::mutex> lock(modelosHFMutex_);
            filas.push_back(ftxui::text("  Error en la instalación") | ftxui::color(kError) | ftxui::bold);
            filas.push_back(ftxui::separator());
            if (!modelosHFError_.empty()) {
                filas.push_back(ftxui::text("  " + modelosHFError_) | ftxui::color(kError));
            }
            filas.push_back(ftxui::separator());
            filas.push_back(ftxui::text("  Pulsa Esc para volver a la lista de variantes") | ftxui::color(kTextoTenue));
        }
    }

    // Contenido según estado
    const int visibles = filasVisibles() - (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS ? 5 : 7);

    if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
        // Lista de modelos
        if (!modelosHFItems_.empty()) {
            const int total = static_cast<int>(modelosHFItems_.size());
            const int fin = std::min(total, modelosHFDesplazamiento_ + visibles);
            for (int i = modelosHFDesplazamiento_; i < fin; ++i) {
                const auto& m = modelosHFItems_[i];
                size_t vcount = m.variants.size();
                std::string linea = "  " + m.id + " (" + std::to_string(vcount) + (vcount ? " variantes" : " sin variantes") + ")";
                ftxui::Element fila = ftxui::text(recortar(linea, anchoTerminal() - 4));
                if (i == modelosHFSeleccion_) {
                    fila = fila | ftxui::bgcolor(kFondoSeleccion) | ftxui::bold;
                }
                filas.push_back(std::move(fila));
            }
        } else if (modelosHFBuscando_) {
            filas.push_back(ftxui::text("  Buscando...") | ftxui::color(kAcento));
        } else if (!modelosHFError_.empty()) {
            filas.push_back(ftxui::text("  Error: " + modelosHFError_) | ftxui::color(kError));
        } else {
            filas.push_back(ftxui::text("  Escribe un término y pulsa Enter para buscar") | ftxui::color(kTextoTenue));
        }
    } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_VARIANTES) {
        // Lista de variantes del modelo seleccionado
        if (modelosHFVariantsCargando_) {
            filas.push_back(ftxui::text("  Cargando variantes...") | ftxui::color(kAcento));
        } else if (!modelosHFError_.empty()) {
            filas.push_back(ftxui::text("  Error: " + modelosHFError_) | ftxui::color(kError));
        } else if (!modelosHFVariants_.empty()) {
            const int total = static_cast<int>(modelosHFVariants_.size());
            const int fin = std::min(total, modelosHFDesplazamiento_ + visibles);
            for (int i = modelosHFDesplazamiento_; i < fin; ++i) {
                const auto& v = modelosHFVariants_[i];
                std::string linea = "  " + v.filename + "  |  " + 
                                    (v.quantization.empty() ? "?" : v.quantization) + "  |  " + 
                                    (v.sizeGb > 0 ? std::to_string(v.sizeGb).substr(0, 4) + " GB" : "? GB");
                ftxui::Element fila = ftxui::text(recortar(linea, anchoTerminal() - 4));
                if (i == modelosHFVariantSeleccion_) {
                    fila = fila | ftxui::bgcolor(kFondoSeleccion) | ftxui::bold | ftxui::color(kExito);
                }
                filas.push_back(std::move(fila));
            }
        } else {
            filas.push_back(ftxui::text("  Sin variantes") | ftxui::color(kTextoTenue));
        }
    }

    return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel);
}


}  // namespace funshi
