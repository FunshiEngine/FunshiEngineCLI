/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — dibujo del panel de ayuda navegable (?).
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/TemaTui.h"
#include "Tui/Terminal/UtilTui.h"
#include "Tui/Vistas/AyudaComandos.h"
#include "Agente/LLM/Local/LlamaBinarios.h"

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


ftxui::Element AplicacionTui::panelAyuda() {
    const auto& lista = ayudaComandos();
    const int total = static_cast<int>(lista.size());
    const int visibles = filasVisibles();
    if (total > 0) ayudaSeleccion_ = std::max(0, std::min(total - 1, ayudaSeleccion_));

    // La lista se desplaza para que el comando elegido quede a la vista.
    if (ayudaSeleccion_ < ayudaDesplazamiento_) ayudaDesplazamiento_ = ayudaSeleccion_;
    if (ayudaSeleccion_ >= ayudaDesplazamiento_ + visibles) {
        ayudaDesplazamiento_ = ayudaSeleccion_ - visibles + 1;
    }
    ayudaDesplazamiento_ = std::max(0, std::min(ayudaDesplazamiento_, std::max(0, total - visibles)));

    // Columna izquierda: lista navegable de comandos.
    std::vector<ftxui::Element> filasLista;
    const int fin = std::min(total, ayudaDesplazamiento_ + visibles);
    for (int i = ayudaDesplazamiento_; i < fin; ++i) {
        const bool elegido = i == ayudaSeleccion_;
        ftxui::Element fila = ftxui::text(std::string(elegido ? " > " : "   ") +
                                          recortar(lista[i].comando, kAnchoListaAyuda - 4));
        if (elegido) fila = fila | ftxui::bgcolor(kFondoSeleccion) | ftxui::bold;
        filasLista.push_back(std::move(fila));
    }
    ftxui::Element columnaLista = ftxui::vbox(std::move(filasLista)) |
                                  ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kAnchoListaAyuda);

    // Columna derecha: uso del comando elegido.
    std::vector<ftxui::Element> detalle;
    if (total > 0) {
        const AyudaComando& ayuda = lista[ayudaSeleccion_];
        detalle.push_back(ftxui::text(" " + ayuda.comando) | ftxui::bold | ftxui::color(kAcento));
        detalle.push_back(ftxui::paragraph(" " + ayuda.descripcion));
        detalle.push_back(ftxui::text(""));
        detalle.push_back(ftxui::text(" Argumentos") | ftxui::bold);
        if (ayuda.usaProyecto) {
            const std::string actual =
                proyectoActivo_.empty() ? "ninguno activo" : "activo: " + proyectoActivo_;
            detalle.push_back(ftxui::hbox({
                ftxui::text("  project") | ftxui::color(kAdvertencia),
                ftxui::text("  texto  ") | ftxui::color(kTextoTenue),
                ftxui::paragraph("Proyecto; si no se indica se usa el proyecto activo (" +
                                 actual + ")") | ftxui::flex,
            }));
        }
        if (ayuda.argumentos.empty() && !ayuda.usaProyecto) {
            detalle.push_back(ftxui::text("  (ninguno)") | ftxui::color(kTextoTenue));
        }
        for (const ArgumentoAyuda& arg : ayuda.argumentos) {
            detalle.push_back(ftxui::hbox({
                ftxui::text("  " + arg.nombre) |
                    ftxui::color(arg.requerido ? kAdvertencia : kTexto),
                ftxui::text("  " + arg.tipo + (arg.requerido ? "  · requerido  " : "  · opcional  ")) |
                    ftxui::color(kTextoTenue),
                ftxui::paragraph(arg.descripcion) | ftxui::flex,
            }));
        }
        detalle.push_back(ftxui::text(""));
        detalle.push_back(ftxui::text(" Uso") | ftxui::bold);
        std::string uso = "  " + ayuda.comando;
        for (const ArgumentoAyuda& arg : ayuda.argumentos) {
            uso += arg.requerido ? " " + arg.nombre + "=<" + arg.nombre + ">"
                                 : " [" + arg.nombre + "=...]";
        }
        detalle.push_back(ftxui::paragraph(uso));
        detalle.push_back(ftxui::text(""));
        detalle.push_back(ftxui::text(" Ejemplo") | ftxui::bold);
        detalle.push_back(ftxui::paragraph("  " + ayuda.ejemplo) | ftxui::color(kExito));
        detalle.push_back(ftxui::text(""));
        detalle.push_back(ftxui::text(" Enter copia el ejemplo a la caja de comandos.") |
                          ftxui::color(kTextoTenue));
    }
    detalle.push_back(ftxui::filler());
    detalle.push_back(ftxui::separator());
    detalle.push_back(ftxui::paragraph(
                          " Sintaxis: clave=valor. Texto con espacios entre comillas; "
                          "true/false son booleanos; listas y objetos en JSON sin "
                          "espacios fuera de comillas, ej. position=[0,1,0].") |
                      ftxui::color(kTextoTenue));

    std::vector<ftxui::Element> filas;
    filas.push_back(ftxui::hbox({
        ftxui::text(" Ayuda · comandos del catálogo  (" + std::to_string(total) + ")") |
            ftxui::bold,
        ftxui::filler(),
        ftxui::text(std::to_string(ayudaSeleccion_ + 1) + " de " + std::to_string(total) + " ") |
            ftxui::color(kTextoTenue),
    }));
    filas.push_back(ftxui::separator());
    filas.push_back(ftxui::hbox({
                        std::move(columnaLista),
                        ftxui::separator(),
                        ftxui::vbox(std::move(detalle)) | ftxui::flex,
                    }) | ftxui::flex);
    return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel);
}

}  // namespace funshi
