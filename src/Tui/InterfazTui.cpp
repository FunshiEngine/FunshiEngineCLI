/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    SPDX-License-Identifier: Apache-2.0
*/
// TUI de la CLI construida sobre FTXUI. Compone una pantalla completa que se
// ajusta al tamaño de la terminal con cuatro piezas: barra superior (versión,
// directorio de trabajo y proyecto activo), panel lateral de dominios, panel
// central con los datos reales del núcleo y barra inferior de atajos y estado.
// Cada carga pide datos a ejecutarSolicitud(): la interfaz no implementa
// lógica de comandos, solo la presenta.
#include "Tui/InterfazTui.h"

#include "Nucleo/NucleoCLI.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef FUNSHI_VERSION
#define FUNSHI_VERSION "0.0.0"
#endif

namespace funshi {

namespace {

// Paleta del tema oscuro de la interfaz.
const ftxui::Color kFondo = ftxui::Color::RGB(18, 18, 24);
const ftxui::Color kFondoBarra = ftxui::Color::RGB(32, 34, 42);
const ftxui::Color kFondoPanel = ftxui::Color::RGB(25, 26, 33);
const ftxui::Color kFondoSeleccion = ftxui::Color::RGB(61, 65, 80);
const ftxui::Color kTexto = ftxui::Color::RGB(216, 219, 228);
const ftxui::Color kTextoTenue = ftxui::Color::RGB(122, 126, 140);
const ftxui::Color kAcento = ftxui::Color::RGB(97, 175, 239);
const ftxui::Color kExito = ftxui::Color::RGB(152, 195, 121);
const ftxui::Color kAdvertencia = ftxui::Color::RGB(229, 192, 123);
const ftxui::Color kError = ftxui::Color::RGB(224, 108, 117);

// Dominios de la TUI, en el orden del panel lateral.
enum class SeccionTui { Proyectos = 0, Escena, Scripts, Diagnosticos };

// Nombre de cada sección para mostrar en la interfaz.
const char* nombreSeccion(SeccionTui seccion) {
    switch (seccion) {
        case SeccionTui::Proyectos: return "Proyectos";
        case SeccionTui::Escena: return "Escena";
        case SeccionTui::Scripts: return "Scripts";
        case SeccionTui::Diagnosticos: return "Diagnósticos";
    }
    return "";
}

// Comando del catálogo que puebla cada sección.
const char* comandoDeSeccion(SeccionTui seccion) {
    switch (seccion) {
        case SeccionTui::Proyectos: return "project.list";
        case SeccionTui::Escena: return "scene.list";
        case SeccionTui::Scripts: return "script.list";
        case SeccionTui::Diagnosticos: return "diagnostics.list";
    }
    return "";
}

// Campo del result donde cada sección encuentra su array de ítems.
const char* campoItemsDeSeccion(SeccionTui seccion) {
    switch (seccion) {
        case SeccionTui::Proyectos: return "projects";
        case SeccionTui::Escena: return "scenes";
        case SeccionTui::Scripts: return "scripts";
        case SeccionTui::Diagnosticos: return "items";
    }
    return "";
}

std::size_t indiceSeccion(SeccionTui seccion) {
    return static_cast<std::size_t>(seccion);
}

// Recorta un texto al ancho indicado, sin cortar a mitad de un carácter
// UTF-8, y agrega puntos de continuación si se recortó.
std::string recortar(const std::string& texto, std::size_t anchoMaximo) {
    if (texto.size() <= anchoMaximo) return texto;
    if (anchoMaximo <= 3) return texto.substr(0, anchoMaximo);
    std::size_t corte = anchoMaximo - 3;
    while (corte > 0 && (static_cast<unsigned char>(texto[corte]) & 0xC0) == 0x80) {
        --corte;  // Retrocede hasta el inicio del carácter UTF-8.
    }
    return texto.substr(0, corte) + "...";
}

// Estado cargado de una sección: sus ítems, la fila seleccionada y el último
// error de carga, si lo hubo.
struct EstadoSeccion {
    nlohmann::json items = nlohmann::json::array();
    std::string error;
    int seleccion = 0;
};

// Aplicación principal: un componente FTXUI que mantiene el estado de
// navegación, pide datos al núcleo y los renderiza.
class AplicacionTui : public ftxui::ComponentBase {
 public:
    explicit AplicacionTui(ftxui::ScreenInteractive* pantalla) : pantalla_(pantalla) {
        recargarSeccionActual();
    }

    ftxui::Element OnRender() override {
        return ftxui::vbox({
                   barraSuperior(),
                   ftxui::hbox({
                       panelLateral(),
                       ftxui::separator(),
                       panelCentral(),
                   }) | ftxui::flex,
                   ftxui::separator(),
                   barraInferior(),
               }) | ftxui::color(kTexto) | ftxui::bgcolor(kFondo);
    }

    bool OnEvent(ftxui::Event evento) override {
        if (evento == ftxui::Event::Character('q') || evento == ftxui::Event::Escape ||
            evento == ftxui::Event::Character('\x03')) {
            pantalla_->Exit();
            return true;
        }
        if (evento == ftxui::Event::Character('r')) {
            recargarSeccionActual();
            return true;
        }
        if (evento == ftxui::Event::ArrowUp) {
            moverSeleccion(-1);
            return true;
        }
        if (evento == ftxui::Event::ArrowDown) {
            moverSeleccion(1);
            return true;
        }
        if (evento == ftxui::Event::ArrowLeft || evento == ftxui::Event::Tab) {
            cambiarSeccion(-1);
            return true;
        }
        if (evento == ftxui::Event::ArrowRight) {
            cambiarSeccion(1);
            return true;
        }
        if (evento == ftxui::Event::Return) {
            accionar();
            return true;
        }
        return false;
    }

 private:
    // Sección actualmente visible.
    SeccionTui seccionActual() const {
        return static_cast<SeccionTui>(indiceSeccion_);
    }

    const EstadoSeccion& estadoActual() const {
        return estados_[indiceSeccion(seccionActual())];
    }

    EstadoSeccion& estadoActual() {
        return estados_[indiceSeccion(seccionActual())];
    }

    // Ejecuta un comando del catálogo contra el núcleo y devuelve su
    // resultado. La única vía de datos de la interfaz.
    static ResultadoComando llamar(const std::string& comando,
                                   const nlohmann::json& argumentos) {
        Solicitud solicitud;
        solicitud.comando = comando;
        solicitud.argumentos = argumentos;
        return ejecutarSolicitud(solicitud);
    }

    // Pide los datos de la sección actual al núcleo y actualiza su estado y
    // la barra de estado con el desenlace.
    void recargarSeccionActual() {
        const SeccionTui seccion = seccionActual();
        EstadoSeccion nuevo;

        nlohmann::json argumentos = nlohmann::json::object();
        if (seccion != SeccionTui::Proyectos) {
            if (proyectoActivo_.empty()) {
                nuevo.error = "Selecciona un proyecto en la sección Proyectos (Enter)";
                estados_[indiceSeccion(seccion)] = std::move(nuevo);
                mensaje_ = "Sin proyecto activo";
                mensajeEsError_ = true;
                return;
            }
            argumentos["project"] = proyectoActivo_;
        }

        ResultadoComando resultado = llamar(comandoDeSeccion(seccion), argumentos);
        if (resultado.respuesta.correcta) {
            const char* campo = campoItemsDeSeccion(seccion);
            nuevo.items = resultado.respuesta.resultado.contains(campo)
                              ? resultado.respuesta.resultado[campo]
                              : nlohmann::json::array();
            if (!nuevo.items.is_array()) nuevo.items = nlohmann::json::array();
            mensaje_ = std::string(comandoDeSeccion(seccion)) + ": " +
                       std::to_string(nuevo.items.size()) + " elemento(s)";
            mensajeEsError_ = false;
        } else {
            nuevo.error = resultado.respuesta.codigoError + ": " +
                          resultado.respuesta.mensajeError;
            mensaje_ = nuevo.error;
            mensajeEsError_ = true;
        }
        estados_[indiceSeccion(seccion)] = std::move(nuevo);
    }

    // Cambia de sección reciclando la selección y recargando sus datos, de
    // modo que siempre se muestre el estado real del disco.
    void cambiarSeccion(int delta) {
        const int cantidad = 4;
        indiceSeccion_ = (indiceSeccion_ + delta + cantidad) % cantidad;
        recargarSeccionActual();
    }

    void moverSeleccion(int delta) {
        EstadoSeccion& estado = estadoActual();
        if (estado.items.empty()) return;
        const int tope = static_cast<int>(estado.items.size()) - 1;
        estado.seleccion = std::max(0, std::min(tope, estado.seleccion + delta));
    }

    // Acción de Enter sobre la fila seleccionada de la sección actual.
    void accionar() {
        const EstadoSeccion& estado = estadoActual();
        if (estado.items.empty() || estado.seleccion >= static_cast<int>(estado.items.size())) {
            return;
        }
        const nlohmann::json& item = estado.items[estado.seleccion];
        switch (seccionActual()) {
            case SeccionTui::Proyectos: {
                proyectoActivo_ = item.value("name", std::string{});
                mensaje_ = "Proyecto activo: " + proyectoActivo_;
                mensajeEsError_ = false;
                break;
            }
            case SeccionTui::Escena: {
                mensaje_ = "Escena: " + item.value("name", std::string{}) + " (" +
                           item.value("file", std::string{}) + ")";
                mensajeEsError_ = false;
                break;
            }
            case SeccionTui::Scripts: {
                mensaje_ = "Script: " + item.value("name", std::string{});
                mensajeEsError_ = false;
                break;
            }
            case SeccionTui::Diagnosticos: {
                mensaje_ = item.value("message", std::string{});
                mensajeEsError_ =
                    item.value("level", std::string{}) == "ERROR" ||
                    item.value("level", std::string{}) == "FATAL";
                break;
            }
        }
    }

    // ---- Render ----

    ftxui::Element barraSuperior() {
        const std::string cwd = std::filesystem::current_path().string();
        std::string contexto = cwd;
        if (!proyectoActivo_.empty()) contexto += "  ·  proyecto: " + proyectoActivo_;
        return ftxui::hbox({
                   ftxui::text(" FunshiCLI " FUNSHI_VERSION) | ftxui::bold | ftxui::color(kAcento),
                   ftxui::filler(),
                   ftxui::text(" " + recortar(contexto, static_cast<std::size_t>(
                                                           std::max(20, ftxui::Terminal::Size().dimx - 34))) +
                               " ") |
                       ftxui::color(kTextoTenue),
               }) | ftxui::bgcolor(kFondoBarra);
    }

    ftxui::Element panelLateral() {
        std::vector<ftxui::Element> filas;
        filas.push_back(ftxui::text(" Dominios") | ftxui::bold | ftxui::color(kAcento));
        filas.push_back(ftxui::separator());
        for (int i = 0; i < 4; ++i) {
            const SeccionTui seccion = static_cast<SeccionTui>(i);
            const bool activa = i == indiceSeccion_;
            ftxui::Element fila =
                ftxui::text(std::string(activa ? "> " : "  ") + nombreSeccion(seccion));
            if (activa) fila = fila | ftxui::bgcolor(kFondoSeleccion) | ftxui::bold;
            filas.push_back(std::move(fila));
        }
        filas.push_back(ftxui::separator());
        filas.push_back(ftxui::text(" Proyecto activo") | ftxui::color(kTextoTenue));
        filas.push_back(ftxui::text(proyectoActivo_.empty()
                                        ? "  (ninguno; Enter en Proyectos)"
                                        : "  " + recortar(proyectoActivo_, 22)) |
                        (proyectoActivo_.empty() ? ftxui::Decorator(ftxui::color(kTextoTenue))
                                                 : ftxui::color(kExito)));
        return ftxui::vbox(std::move(filas)) |
               ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 30) | ftxui::bgcolor(kFondoPanel);
    }

    ftxui::Element panelCentral() {
        const SeccionTui seccion = seccionActual();
        const EstadoSeccion& estado = estadoActual();
        std::vector<ftxui::Element> filas;

        filas.push_back(ftxui::text(" " + std::string(nombreSeccion(seccion)) + "  (" +
                                    std::to_string(estado.items.size()) + ")") |
                        ftxui::bold);
        filas.push_back(ftxui::separator());

        if (!estado.error.empty()) {
            filas.push_back(ftxui::text(" " + estado.error) | ftxui::color(kError));
        } else if (estado.items.empty()) {
            filas.push_back(ftxui::text(" Sin elementos. r para recargar.") |
                            ftxui::color(kTextoTenue));
        } else {
            const std::size_t ancho =
                static_cast<std::size_t>(std::max(20, ftxui::Terminal::Size().dimx - 34));
            for (std::size_t i = 0; i < estado.items.size(); ++i) {
                ftxui::Element fila = filaDeItem(seccion, estado.items[i], ancho);
                if (static_cast<int>(i) == estado.seleccion) {
                    fila = fila | ftxui::bgcolor(kFondoSeleccion);
                }
                filas.push_back(std::move(fila));
            }
        }
        return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel) |
               ftxui::frame;
    }

    // Fila de un ítem, con el formato propio de su sección.
    ftxui::Element filaDeItem(SeccionTui seccion, const nlohmann::json& item,
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

    ftxui::Element barraInferior() {
        ftxui::Element estado = ftxui::text(" " + recortar(mensaje_, 60) + " ") |
                                (mensajeEsError_ ? ftxui::color(kError)
                                                 : ftxui::color(kTextoTenue));
        ftxui::Element atajos =
            ftxui::text(" ↑↓ navegar · ←→/Tab cambiar panel · Enter seleccionar · "
                        "r refrescar · q salir ") |
            ftxui::color(kTextoTenue);
        return ftxui::hbox({std::move(estado), ftxui::filler(), std::move(atajos)}) |
               ftxui::bgcolor(kFondoBarra);
    }

    ftxui::ScreenInteractive* pantalla_ = nullptr;
    std::vector<EstadoSeccion> estados_ = std::vector<EstadoSeccion>(4);
    int indiceSeccion_ = 0;
    std::string proyectoActivo_;
    std::string mensaje_ = "Listo";
    bool mensajeEsError_ = false;
};

}  // namespace

int ejecutarTui() {
    ftxui::ScreenInteractive pantalla = ftxui::ScreenInteractive::Fullscreen();
    auto aplicacion = std::make_shared<AplicacionTui>(&pantalla);
    pantalla.Loop(aplicacion);
    return codigosSalida::kExito;
}

}  // namespace funshi
