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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    // Pestañas: «Descargados» (modelos locales) y «Buscar en HF».
    {
        const bool enLocales = modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES ||
                               (modelosHFEstado_ != ModeloHFEstado::LISTA_MODELOS &&
                                modelosHFEstado_ != ModeloHFEstado::LISTA_VARIANTES &&
                                modelosHFEstadoRetorno_ == ModeloHFEstado::LISTA_LOCALES);
        auto pestana = [](const std::string& texto, bool activa) {
            ftxui::Element e = ftxui::text(" " + texto + " ");
            return activa ? (e | ftxui::bold | ftxui::bgcolor(kFondoSeleccion) | ftxui::color(kExito))
                          : (e | ftxui::color(kTextoTenue));
        };
        filas.push_back(ftxui::hbox({
            ftxui::text(" Modelos ") | ftxui::bold,
            pestana("Descargados", enLocales),
            ftxui::text(" "),
            pestana("Buscar en Hugging Face", !enLocales),
            ftxui::filler(),
            ftxui::text("Tab cambia ") | ftxui::color(kTextoTenue),
        }));
    }
    filas.push_back(ftxui::separator());

    // Caja de búsqueda (solo en modo LISTA_MODELOS)
    if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
        ftxui::Element entradaBuscar = entradaModelosHF_->Render();
        filas.push_back(ftxui::hbox({
            ftxui::text(focoEnModelosHF_ ? " > " : " / ") | ftxui::color(kAcento),
            std::move(entradaBuscar) | ftxui::flex,
        }) | ftxui::bgcolor(kFondoBarra));

        filas.push_back(ftxui::text(" Enter busca / elige modelo · ↑↓ navega · Tab descargados · Esc vuelve ") |
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
    } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
        if (modelosHFLocalesCargando_) {
            filas.push_back(ftxui::text("  Buscando modelos locales...") | ftxui::color(kAcento));
        } else {
            filas.push_back(ftxui::text("  Disponibles: " + std::to_string(modelosHFLocales_.size())) |
                            ftxui::color(kAcento));
        }
        filas.push_back(ftxui::separator());
        filas.push_back(ftxui::text("  Enter usa el modelo · r recarga · Tab busca en HF · Esc sale") |
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
                static const char* const frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼",
                                                     "⠴", "⠦", "⠧", "⠇", "⠏"};
                bar += frames[modelosHFFrame_ % 10];
                for (int i = 1; i < barWidth; ++i) bar += " ";
                bar += "] trabajando...";
            } else {
                int filled = static_cast<int>(modelosHFProgreso_ * barWidth);
                for (int i = 0; i < barWidth; ++i) {
                    bar += (i < filled) ? "=" : " ";
                }
                char pct[16];
                std::snprintf(pct, sizeof(pct), "%.1f%%", std::min(1.0, modelosHFProgreso_) * 100.0);
                bar += "] " + std::string(pct);
            }
            filas.push_back(ftxui::text("  " + bar) | ftxui::color(kAcento));
            // Pista de tecla
            filas.push_back(ftxui::text("  (Esc para volver)") | ftxui::color(kTextoTenue));
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
            filas.push_back(ftxui::text("  Pulsa Esc para volver") | ftxui::color(kTextoTenue));
        }
    }

    // Contenido según estado
    int reserva = 7;
    if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) reserva = 5;
    const int visibles = std::max(1, filasVisibles() - reserva);
    // Ventana visible que siempre contiene a la selección.
    auto ventana = [visibles](int seleccion, int total, int& desplazamiento) {
        if (seleccion < desplazamiento) desplazamiento = seleccion;
        if (seleccion >= desplazamiento + visibles) desplazamiento = seleccion - visibles + 1;
        desplazamiento = std::max(0, std::min(desplazamiento, std::max(0, total - visibles)));
        return std::min(total, desplazamiento + visibles);
    };

    if (modelosHFEstado_ == ModeloHFEstado::LISTA_MODELOS) {
        // Lista de modelos
        if (!modelosHFItems_.empty()) {
            const int total = static_cast<int>(modelosHFItems_.size());
            const int fin = ventana(modelosHFSeleccion_, total, modelosHFDesplazamiento_);
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
            modelosHFVariantSeleccion_ = std::max(0, std::min(modelosHFVariantSeleccion_, total - 1));
            const int fin = ventana(modelosHFVariantSeleccion_, total, modelosHFVariantDesplazamiento_);
            for (int i = modelosHFVariantDesplazamiento_; i < fin; ++i) {
                const auto& v = modelosHFVariants_[i];
                const bool descargada = i < static_cast<int>(modelosHFVariantDescargada_.size()) &&
                                        modelosHFVariantDescargada_[i];
                char gb[32] = "? GB";
                if (v.sizeGb > 0) std::snprintf(gb, sizeof(gb), "%.2f GB", v.sizeGb);
                std::string linea = std::string(descargada ? "  ✓ " : "    ") + v.filename + "  |  " +
                                    (v.quantization.empty() ? "?" : v.quantization) + "  |  " + gb +
                                    (descargada ? "  |  descargado" : "");
                ftxui::Element fila = ftxui::text(recortar(linea, anchoTerminal() - 4));
                if (i == modelosHFVariantSeleccion_) {
                    fila = fila | ftxui::bgcolor(kFondoSeleccion) | ftxui::bold | ftxui::color(kExito);
                }
                filas.push_back(std::move(fila));
            }
        } else {
            filas.push_back(ftxui::text("  Sin variantes") | ftxui::color(kTextoTenue));
        }
    } else if (modelosHFEstado_ == ModeloHFEstado::LISTA_LOCALES) {
        if (modelosHFLocalesCargando_) {
            filas.push_back(ftxui::text("  Cargando modelos locales...") | ftxui::color(kAcento));
        } else if (!modelosHFError_.empty()) {
            filas.push_back(ftxui::text("  " + modelosHFError_) | ftxui::color(kError));
        } else if (!modelosHFLocales_.empty()) {
            auto tamanoLegible = [](std::uintmax_t bytes) -> std::string {
                const char* suf[] = {"B", "KB", "MB", "GB"};
                double v = static_cast<double>(bytes);
                int i = 0;
                while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
                char buf[32];
                std::snprintf(buf, sizeof(buf), (i >= 2 ? "%.2f %s" : "%.0f %s"), v, suf[i]);
                return std::string(buf);
            };
            const int total = static_cast<int>(modelosHFLocales_.size());
            const int visiblesLista = std::max(1, visibles);
            if (modelosHFLocalSeleccion_ < modelosHFLocalDesplazamiento_) {
                modelosHFLocalDesplazamiento_ = modelosHFLocalSeleccion_;
            }
            if (modelosHFLocalSeleccion_ >= modelosHFLocalDesplazamiento_ + visiblesLista) {
                modelosHFLocalDesplazamiento_ = modelosHFLocalSeleccion_ - visiblesLista + 1;
            }
            modelosHFLocalDesplazamiento_ = std::max(
                0, std::min(modelosHFLocalDesplazamiento_, std::max(0, total - visiblesLista)));
            const int inicio = modelosHFLocalDesplazamiento_;
            const int fin = std::min(total, inicio + visiblesLista);
            // Carpeta con $HOME abreviado a ~ (dos archivos pueden llamarse igual).
            std::string home;
            if (const char* h = std::getenv("HOME")) home = h;
#ifdef _WIN32
            if (home.empty()) if (const char* h = std::getenv("USERPROFILE")) home = h;
#endif
            for (int i = inicio; i < fin; ++i) {
                const auto& p = modelosHFLocales_[i];
                const bool completo = i >= static_cast<int>(modelosHFLocalCompleto_.size()) ||
                                      modelosHFLocalCompleto_[i];
                const bool activo = i == modelosHFLocalActivo_;
                std::error_code ec;
                auto sz = std::filesystem::file_size(p, ec);
                std::string info = ec ? "" : ("  |  " + tamanoLegible(sz));
                if (activo) info += "  |  activo";
                if (!completo) info += "  |  incompleto";
                std::string dir = p.parent_path().string();
                if (!home.empty() && dir.rfind(home, 0) == 0) dir = "~" + dir.substr(home.size());
                if (!dir.empty()) info += "  |  " + dir;
                std::string linea = std::string(activo ? "  ● " : (completo ? "    " : "  ✗ ")) +
                                    p.filename().string() + info;
                ftxui::Element fila = ftxui::text(recortar(linea, anchoTerminal() - 4));
                if (activo) fila = fila | ftxui::color(kAcento);
                if (!completo) fila = fila | ftxui::color(kTextoTenue);
                if (i == modelosHFLocalSeleccion_) {
                    fila = fila | ftxui::bgcolor(kFondoSeleccion) | ftxui::bold | ftxui::color(kExito);
                }
                filas.push_back(std::move(fila));
            }
        } else {
            filas.push_back(ftxui::text("  Aún no hay modelos descargados. Pulsa Tab para buscar en Hugging Face.") |
                            ftxui::color(kTextoTenue));
        }
    }

    return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel);
}


}  // namespace funshi
