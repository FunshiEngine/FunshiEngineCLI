/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — módulo render
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


ftxui::Element AplicacionTui::barraSuperior() {
    const std::string cwd = std::filesystem::current_path().string();
    std::string contexto = cwd;
    if (!proyectoActivo_.empty()) contexto += "  ·  proyecto: " + proyectoActivo_;
    
    // Indicador de servidor local
    std::string servidorLocal = "";
    std::string modeloActivo = silvy_.modeloActivo();
    if (!modeloActivo.empty()) {
        servidorLocal = "  ·  🟢 LOCAL: " + modeloActivo;
    } else if (silvy_.tieneLlm()) {
        servidorLocal = "  ·  🔵 REMOTE";
    }
    
    return ftxui::hbox({
               ftxui::text(" FunshiCLI " FUNSHI_VERSION) | ftxui::bold | ftxui::color(kAcento),
               ftxui::filler(),
               ftxui::text(" " + recortar(contexto + servidorLocal, static_cast<std::size_t>(
                                                       std::max(20, anchoTerminal() - 34))) +
                           " ") |
                   ftxui::color(kTextoTenue),
           }) | ftxui::bgcolor(kFondoBarra);
}

ftxui::Element AplicacionTui::panelLateral() {
    std::vector<ftxui::Element> filas;
    filas.push_back(ftxui::text(" Dominios") | ftxui::bold | ftxui::color(kAcento));
    filas.push_back(ftxui::separator());
    for (int i = 0; i < kCantidadSecciones; ++i) {
        const SeccionTui seccion = static_cast<SeccionTui>(i);
        const bool activa = i == indiceSeccion_;
        ftxui::Element fila =
            ftxui::text(std::string(activa ? "> " : "  ") + nombreSeccion(seccion));
        if (activa) fila = fila | ftxui::bgcolor(kFondoSeleccion) | ftxui::bold;
        filas.push_back(std::move(fila));
    }
    filas.push_back(ftxui::separator());
    filas.push_back(ftxui::text(" Proyecto activo") | ftxui::color(kTextoTenue));
    if (proyectoActivo_.empty()) {
        filas.push_back(ftxui::text("  (ninguno)") | ftxui::color(kTextoTenue));
        filas.push_back(ftxui::text("  Enter en Proyectos") | ftxui::color(kTextoTenue));
    } else {
        filas.push_back(ftxui::text("  " + recortar(proyectoActivo_, 24)) |
                        ftxui::color(kExito));
    }
    return ftxui::vbox(std::move(filas)) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kAnchoPanelLateral) |
           ftxui::bgcolor(kFondoPanel);
}

ftxui::Element AplicacionTui::panelCentral() {
    if (ayudaVisible_) {
        return panelAyuda();
    }
    const SeccionTui seccion = seccionActual();
    if (seccion == SeccionTui::Chat) {
        return panelChat();
    }
    if (seccion == SeccionTui::ModelosHF) {
        return panelModelosHF();
    }
    EstadoSeccion& estado = estadoActual();
    const int visibles = filasVisibles();
    const int total = static_cast<int>(estado.items.size());

    // La lista se desplaza para que la fila seleccionada quede a la vista.
    if (estado.seleccion < estado.desplazamiento) {
        estado.desplazamiento = estado.seleccion;
    }
    if (estado.seleccion >= estado.desplazamiento + visibles) {
        estado.desplazamiento = estado.seleccion - visibles + 1;
    }
    if (estado.desplazamiento > std::max(0, total - visibles)) {
        estado.desplazamiento = std::max(0, total - visibles);
    }
    if (estado.desplazamiento < 0) {
        estado.desplazamiento = 0;
    }

    std::vector<ftxui::Element> filas;
    filas.push_back(ftxui::hbox({
                       ftxui::text(" " + std::string(nombreSeccion(seccion)) + "  (" +
                                   std::to_string(total) + ")") |
                           ftxui::bold,
                       ftxui::filler(),
                       ftxui::text(total > visibles
                                       ? std::to_string(estado.desplazamiento + 1) + "-" +
                                             std::to_string(std::min(total, estado.desplazamiento + visibles)) +
                                             " de " + std::to_string(total)
                                       : "") |
                           ftxui::color(kTextoTenue),
                   }));
    filas.push_back(ftxui::separator());

    if (!estado.error.empty()) {
        filas.push_back(ftxui::text(" " + estado.error) | ftxui::color(kError));
    } else if (estado.items.empty()) {
        filas.push_back(ftxui::text(" Sin elementos. r para recargar.") |
                        ftxui::color(kTextoTenue));
    } else {
        const std::size_t ancho = static_cast<std::size_t>(
            std::max(20, anchoTerminal() - kColumnaPanelCentral - 2));
        const int fin = std::min(total, estado.desplazamiento + visibles);
        for (int i = estado.desplazamiento; i < fin; ++i) {
            ftxui::Element fila = filaDeItem(seccion, estado.items[i], ancho);
            if (i == estado.seleccion) {
                fila = fila | ftxui::bgcolor(kFondoSeleccion);
            }
            filas.push_back(std::move(fila));
        }
    }
    return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel);
}

ftxui::Element AplicacionTui::panelAyuda() {
    std::vector<ftxui::Element> filas;
    filas.push_back(ftxui::text(" Comandos del catalogo") | ftxui::bold);
    filas.push_back(ftxui::separator());
    for (const ComandoCLI comando : todosLosComandos()) {
        filas.push_back(ftxui::text("  " + std::string(textoDesdeComando(comando))) |
                        ftxui::color(kTexto));
    }
    filas.push_back(ftxui::separator());
    filas.push_back(ftxui::text(" En la caja de comandos: nombre argumentos, por") |
                    ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text(" ejemplo: project.create name=MiJuego dimension=2") |
                    ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text(" Una tecla cierra esta ayuda.") | ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text("") | ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text(" Teclas: flechas mover y cambiar de seccion,") |
                    ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text(" avpag y repag recorren la lista, inicio y fin") |
                    ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text(" van a sus extremos, enter usa el elemento.") |
                    ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text(" En la caja de comandos, esc vuelve al panel") |
                    ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text(" y las flechas arriba y abajo recorren el") |
                    ftxui::color(kTextoTenue));
    filas.push_back(ftxui::text(" historial de lo escrito.") | ftxui::color(kTextoTenue));
    return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel);
}

ftxui::Element AplicacionTui::filaDeItem(SeccionTui seccion, const nlohmann::json& item,
                          std::size_t ancho) {
    switch (seccion) {
        case SeccionTui::Proyectos: {
            const std::string nombre = item.value("name", std::string{});
            const int dimension = item.value("dimension", 3);
            const bool valido = item.value("valid", true);
            std::string linea = nombre;
            if (nombre == proyectoActivo_) linea = "* " + linea;
            std::string relleno(ancho > linea.size() + 14 ? ancho - linea.size() - 14 : 1, ' ');
            linea += relleno + (dimension == 2 ? "2D  " : "3D  ");
            ftxui::Element marca = valido ? ftxui::text("valido") | ftxui::color(kExito)
                                          : ftxui::text("invalido") | ftxui::color(kError);
            return ftxui::hbox({
                ftxui::text("  " + recortar(linea, ancho)),
                std::move(marca),
            });
        }
        case SeccionTui::Escena: {
            const std::string nombre = item.value("name", std::string{});
            const std::string archivo = item.value("file", std::string{});
            const long long tamano = item.value("size", 0LL);
            std::string linea = "  " + nombre + "  (" + archivo + ")";
            return ftxui::hbox({
                ftxui::text(recortar(linea, ancho > 12 ? ancho - 12 : ancho)),
                ftxui::filler(),
                ftxui::text(std::to_string(tamano) + " B ") | ftxui::color(kTextoTenue),
            });
        }
        case SeccionTui::Scripts: {
            const std::string nombre = item.value("name", std::string{});
            const std::string lenguaje = item.value("language", std::string{});
            const long long tamano = item.value("size", 0LL);
            std::string linea = "  " + nombre;
            return ftxui::hbox({
                ftxui::text(recortar(linea, ancho > 20 ? ancho - 20 : ancho)),
                ftxui::text("[" + lenguaje + "]") | ftxui::color(kAcento),
                ftxui::filler(),
                ftxui::text(std::to_string(tamano) + " B ") | ftxui::color(kTextoTenue),
            });
        }
        case SeccionTui::Diagnosticos: {
            const std::string nivel = item.value("level", std::string{});
            const std::string modulo = item.value("module", std::string{});
            const std::string mensaje = item.value("message", std::string{});
            ftxui::Color colorNivel = kTextoTenue;
            if (nivel == "ERROR" || nivel == "FATAL") {
                colorNivel = kError;
            } else if (nivel == "WARNING" || nivel == "WARN") {
                colorNivel = kAdvertencia;
            }
            const std::string linea = "  " + recortar(nivel, 8) + "  " +
                                      recortar(modulo, 14) + "  " + mensaje;
            return ftxui::text(recortar(linea, ancho)) | ftxui::color(colorNivel);
        }
    }
    return ftxui::text("");
}

ftxui::Element AplicacionTui::cajaComandos() {
    ftxui::Element entrada = entrada_->Render();
    return ftxui::hbox({
               ftxui::text(focoEnEntrada_ ? " > " : " / ") | ftxui::color(kAcento),
               std::move(entrada) | ftxui::flex,
           }) | ftxui::bgcolor(kFondoBarra);
}

}  // namespace funshi
