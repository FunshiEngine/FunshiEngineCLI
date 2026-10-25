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
// ajusta al tamaño de la terminal con cinco piezas: barra superior (versión,
// directorio de trabajo y proyecto activo), panel lateral de dominios, panel
// central con los datos reales del núcleo, caja de comandos y barra de
// estado. Cada carga pide datos a ejecutarSolicitud(): la interfaz no
// implementa lógica de comandos, solo la presenta.
#include "Tui/InterfazTui.h"

#include "Tui/EntradaRespaldo.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"

#include "Nucleo/CatalogoComandos.h"
#include "Nucleo/NucleoCLI.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
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
enum class SeccionTui { Proyectos = 0, Escena, Scripts, Diagnosticos, Chat };

// Cantidad de dominios del panel lateral.
constexpr int kCantidadSecciones = 5;

// Ancho del panel lateral, en columnas.
constexpr int kAnchoPanelLateral = 30;

// Columna en la que empieza el panel central (panel lateral + separador).
constexpr int kColumnaPanelCentral = kAnchoPanelLateral + 1;

// Filas que la pantalla reserva fuera del área de paneles: barra superior,
// separador, caja de comandos, separador, mensaje de estado y atajos.
constexpr int kFilasFueraDePaneles = 6;

// Filas que ocupa el encabezado de cada panel: su título y el separador que
// lo sigue.
constexpr int kFilasEncabezado = 2;

// Fila de la pantalla donde empieza el área de paneles, justo bajo la barra
// superior.
constexpr int kFilaPaneles = 1;

// Fila de la pantalla donde empieza la lista de ítems del panel central.
constexpr int kFilaPrimerItem = kFilaPaneles + kFilasEncabezado;

// Fila de la pantalla donde empieza la lista de dominios del panel lateral.
constexpr int kFilaPrimeraSeccion = kFilaPrimerItem;

// Filas que ocupa la barra inferior, contadas desde el final de la pantalla:
// caja de comandos, separador, mensaje y atajos.
constexpr int kFilasBarraInferior = 4;

// Nombre de cada sección para mostrar en la interfaz.
const char* nombreSeccion(SeccionTui seccion) {
    switch (seccion) {
        case SeccionTui::Proyectos: return "Proyectos";
        case SeccionTui::Escena: return "Escena";
        case SeccionTui::Scripts: return "Scripts";
        case SeccionTui::Diagnosticos: return "Diagnósticos";
        case SeccionTui::Chat: return "Chat Silvy";
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
        case SeccionTui::Chat: return "";
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
        case SeccionTui::Chat: return "";
    }
    return "";
}

std::size_t indiceSeccion(SeccionTui seccion) {
    return static_cast<std::size_t>(seccion);
}

// Índice del byte donde el texto alcanza el ancho indicado en columnas de
// pantalla, sin partir un carácter UTF-8.
std::size_t byteDeAncho(const std::string& texto, std::size_t anchoMaximo) {
    std::size_t ancho = 0;
    std::size_t i = 0;
    while (i < texto.size()) {
        // Longitud del carácter UTF-8 en bytes.
        const unsigned char byte = static_cast<unsigned char>(texto[i]);
        std::size_t largo = 1;
        if ((byte & 0xE0) == 0xC0) {
            largo = 2;
        } else if ((byte & 0xF0) == 0xE0) {
            largo = 3;
        } else if ((byte & 0xF8) == 0xF0) {
            largo = 4;
        }
        if (i + largo > texto.size()) break;
        const int anchoCaracter = ftxui::string_width(texto.substr(i, largo));
        if (ancho + static_cast<std::size_t>(anchoCaracter) > anchoMaximo) break;
        ancho += static_cast<std::size_t>(anchoCaracter);
        i += largo;
    }
    return i;
}

// Recorta un texto al ancho indicado en columnas de pantalla, sin cortar a
// mitad de un carácter UTF-8, y agrega puntos de continuación si se recortó.
std::string recortar(const std::string& texto, std::size_t anchoMaximo) {
    if (ftxui::string_width(texto) <= static_cast<int>(anchoMaximo)) return texto;
    if (anchoMaximo <= 3) return texto.substr(0, byteDeAncho(texto, anchoMaximo));
    return texto.substr(0, byteDeAncho(texto, anchoMaximo - 3)) + "...";
}

// Ancho útil de la terminal, con un mínimo para que la interfaz no se
// descomponga en ventanas muy angostas.
int anchoTerminal() {
    return std::max(40, ftxui::Terminal::Size().dimx);
}

// Atajos de la barra inferior, de la versión más completa a la más corta: se
// usa la primera que cabe en el ancho de la terminal, para que en ventanas
// angostas la línea no quede cortada a media palabra.
const std::vector<std::string>& atajosBarraInferior() {
    static const std::vector<std::string> atajos = {
        " ↑↓ mover · ←→ sección · enter usar · r recargar · / comando · ? ayuda · esc/q salir ",
        " ↑↓ mover · ←→ sección · enter usar · / comando · ? ayuda · esc/q salir ",
        " ↑↓ mover · enter usar · / comando · ? ayuda · esc/q salir ",
    };
    return atajos;
}

// Texto de atajos que cabe en la terminal actual.
std::string textoAtajos() {
    const std::size_t ancho = static_cast<std::size_t>(anchoTerminal());
    for (const std::string& atajos : atajosBarraInferior()) {
        if (ftxui::string_width(atajos) <= static_cast<int>(ancho)) return atajos;
    }
    return recortar(atajosBarraInferior().back(), ancho);
}

// Pieza de la línea de comandos: el texto y si venía entrecomillado, para
// distinguir un valor numérico de uno textual.
struct Token {
    std::string texto;
    bool entrecomillado = false;
};

// Divide una línea en piezas, respetando comillas simples y dobles: el
// texto entrecomillado se une al token aunque tenga espacios.
std::vector<Token> tokenizar(const std::string& linea) {
    std::vector<Token> tokens;
    Token actual;
    char comilla = 0;
    for (const char caracter : linea) {
        if (comilla != 0) {
            if (caracter == comilla) {
                comilla = 0;
                continue;
            }
            actual.texto += caracter;
            continue;
        }
        if (caracter == '"' || caracter == '\'') {
            comilla = caracter;
            actual.entrecomillado = true;
            continue;
        }
        if (caracter == ' ' || caracter == '\t') {
            if (!actual.texto.empty()) {
                tokens.push_back(actual);
                actual = Token();
            }
            continue;
        }
        actual.texto += caracter;
    }
    if (!actual.texto.empty()) {
        tokens.push_back(actual);
    }
    return tokens;
}

// Indica si un texto es un entero que cabe en un long long.
bool esEntero(const std::string& texto) {
    if (texto.empty()) return false;
    std::size_t inicio = (texto[0] == '-' || texto[0] == '+') ? 1 : 0;
    if (inicio >= texto.size()) return false;
    for (std::size_t i = inicio; i < texto.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(texto[i]))) return false;
    }
    try {
        static_cast<void>(std::stoll(texto));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// Convierte los tokens "clave=valor" en argumentos de la solicitud. Un valor
// numérico sin comillas se manda como número; entrecomillado, como texto.
bool construirArgumentos(const std::vector<Token>& tokens, nlohmann::json& argumentos,
                          std::string& error) {
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& token = tokens[i].texto;
        const std::size_t separador = token.find('=');
        if (separador == std::string::npos) {
            error = "Argumento sin '=': " + token;
            return false;
        }
        const std::string clave = token.substr(0, separador);
        const std::string valor = token.substr(separador + 1);
        if (clave.empty()) {
            error = "Argumento sin nombre: " + token;
            return false;
        }
        if (!tokens[i].entrecomillado && esEntero(valor)) {
            argumentos[clave] = std::stoll(valor);
        } else {
            argumentos[clave] = valor;
        }
    }
    return true;
}

// Estado cargado de una sección: sus ítems, la fila seleccionada, la fila
// visible más alta y el último error de carga, si lo hubo.
struct EstadoSeccion {
    nlohmann::json items = nlohmann::json::array();
    std::string error;
    int seleccion = 0;
    int desplazamiento = 0;
};

// Aplicación principal: un componente FTXUI que mantiene el estado de
// navegación, pide datos al núcleo y los renderiza.
class AplicacionTui : public ftxui::ComponentBase {
 public:
    AplicacionTui(ftxui::ScreenInteractive* pantalla,
                  Auditoria& auditoria, Harness& harness, Silvy& silvy)
        : pantalla_(pantalla), auditoria_(auditoria),
          harness_(harness), silvy_(silvy) {
        ftxui::InputOption opciones;
        opciones.content = &textoComando_;
        opciones.placeholder = "comando del catalogo, por ejemplo project.list";
        opciones.multiline = false;
        // El comando se ejecuta al recibir Enter, no con on_enter: FTXUI difiere
        // ese callback a la cola de tareas y se ejecutaria con el texto ya
        // modificado si mas teclas llegan en el mismo lote.
        opciones.transform = [](const ftxui::InputState& estado) {
            ftxui::Element elemento = estado.element;
            if (estado.focused) {
                elemento = elemento | ftxui::color(kAcento) | ftxui::bold;
            } else {
                elemento = elemento | ftxui::color(kTextoTenue);
            }
            return elemento;
        };
        entrada_ = ftxui::Input(opciones);
        Add(entrada_);

        // Input dedicado del chat de Silvy (órdenes en lenguaje natural o JSON).
        ftxui::InputOption opcionesChat;
        opcionesChat.content = &textoChat_;
        opcionesChat.placeholder = "orden de Silvy, ej. 'crear proyecto MiJuego 2d'";
        opcionesChat.multiline = false;
        opcionesChat.transform = [](const ftxui::InputState& estado) {
            ftxui::Element elemento = estado.element;
            if (estado.focused) {
                elemento = elemento | ftxui::color(kAcento) | ftxui::bold;
            } else {
                elemento = elemento | ftxui::color(kTextoTenue);
            }
            return elemento;
        };
        entradaChat_ = ftxui::Input(opcionesChat);
        Add(entradaChat_);

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
                   cajaComandos(),
                   ftxui::separator(),
                   ftxui::text(" " + mensaje_ + " ") |
                       (mensajeEsError_ ? ftxui::color(kError) : ftxui::color(kTextoTenue)) |
                       ftxui::bgcolor(kFondoBarra),
                   ftxui::text(textoAtajos()) |
                       ftxui::color(kTextoTenue) | ftxui::bgcolor(kFondoBarra),
               }) | ftxui::color(kTexto) | ftxui::bgcolor(kFondo);
    }

    bool OnEvent(ftxui::Event evento) override {
        if (focoEnChat_) {
            return eventoEnChat(evento);
        }
        if (focoEnEntrada_) {
            return eventoEnEntrada(evento);
        }
        if (ayudaVisible_) {
            // Cualquier tecla o clic cierra la ayuda.
            ayudaVisible_ = false;
            return true;
        }
        if (evento.is_mouse()) {
            return manejarRaton(evento);
        }
        if (evento == ftxui::Event::Character('q') || evento == ftxui::Event::CtrlC ||
            evento == ftxui::Event::Character('\x03')) {
            pantalla_->Exit();
            return true;
        }
        if (evento == ftxui::Event::Escape) {
            pantalla_->Exit();
            return true;
        }
        if (evento == ftxui::Event::Character('r')) {
            recargarSeccionActual();
            return true;
        }
        if (evento == ftxui::Event::Character('/')) {
            if (seccionActual() == SeccionTui::Chat) {
                enfocarChat();
                return true;
            }
            enfocarEntrada();
            return true;
        }
        if (evento == ftxui::Event::Character('?')) {
            ayudaVisible_ = true;
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
        if (evento == ftxui::Event::PageUp) {
            moverSeleccion(-filasVisibles());
            return true;
        }
        if (evento == ftxui::Event::PageDown) {
            moverSeleccion(filasVisibles());
            return true;
        }
        if (evento == ftxui::Event::Home) {
            irAlInicio();
            return true;
        }
        if (evento == ftxui::Event::End) {
            irAlFinal();
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

    // El foco de FTXUI sigue al hijo activo: la caja de comandos cuando está
    // enfocada, ninguno cuando la navegación está en el panel central.
    ftxui::Component ActiveChild() override {
        return focoEnEntrada_ ? entrada_ : nullptr;
    }

 private:
    // ---- Estado ----

    SeccionTui seccionActual() const {
        return static_cast<SeccionTui>(indiceSeccion_);
    }

    const EstadoSeccion& estadoActual() const {
        return estados_[indiceSeccion(seccionActual())];
    }

    EstadoSeccion& estadoActual() {
        return estados_[indiceSeccion(seccionActual())];
    }

    // Filas que puede mostrar la lista de ítems: las que quedan tras quitar
    // la barra superior, la barra inferior y el encabezado del panel.
    int filasVisibles() const {
        return std::max(1, ftxui::Terminal::Size().dimy - kFilasFueraDePaneles -
                                 kFilasEncabezado);
    }

    // Fila donde se dibuja la caja de comandos: la barra inferior ocupa las
    // últimas filas de la pantalla.
    int filaCajaComandos() const {
        return std::max(0, ftxui::Terminal::Size().dimy - kFilasBarraInferior);
    }

    // ---- Datos ----

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
    // la barra de estado con el desenlace. `informar` permite recargar sin
    // pisar el mensaje: al ejecutar un comando desde la caja, el resultado
    // que se quiere ver es el del comando, no el de la recarga.
    void recargarSeccionActual(bool informar = true) {
        const SeccionTui seccion = seccionActual();
        EstadoSeccion& estado = estados_[indiceSeccion(seccion)];
        estado.desplazamiento = 0;

        // La sección Chat no carga datos del núcleo: muestra su propio
        // historial de auditoría de Silvy.
        if (seccion == SeccionTui::Chat) {
            estado.items = nlohmann::json::array();
            estado.error.clear();
            estado.seleccion = 0;
            return;
        }

        nlohmann::json argumentos = nlohmann::json::object();
        if (seccion != SeccionTui::Proyectos) {
            if (proyectoActivo_.empty()) {
                estado.error = "Selecciona un proyecto en la seccion Proyectos (Enter)";
                estado.items = nlohmann::json::array();
                if (informar) {
                    mensaje_ = "Sin proyecto activo";
                    mensajeEsError_ = true;
                }
                return;
            }
            argumentos["project"] = proyectoActivo_;
        }

        ResultadoComando resultado = llamar(comandoDeSeccion(seccion), argumentos);
        if (resultado.respuesta.correcta) {
            const char* campo = campoItemsDeSeccion(seccion);
            estado.items = resultado.respuesta.resultado.contains(campo)
                               ? resultado.respuesta.resultado[campo]
                               : nlohmann::json::array();
            if (!estado.items.is_array()) estado.items = nlohmann::json::array();
            estado.error.clear();
            estado.seleccion = 0;
            if (informar) {
                mensaje_ = std::string(comandoDeSeccion(seccion)) + ": " +
                           std::to_string(estado.items.size()) + " elemento(s)";
                mensajeEsError_ = false;
            }
        } else {
            estado.items = nlohmann::json::array();
            estado.error = resultado.respuesta.codigoError + ": " +
                           resultado.respuesta.mensajeError;
            if (informar) {
                mensaje_ = estado.error;
                mensajeEsError_ = true;
            }
        }
    }

    // Cambia de sección reciclando la selección y recargando sus datos, de
    // modo que siempre se muestre el estado real del disco.
    void cambiarSeccion(int delta) {
        indiceSeccion_ = (indiceSeccion_ + delta + kCantidadSecciones) % kCantidadSecciones;
        recargarSeccionActual();
    }

    public:
    // Cambia a la sección del panel lateral indicada y recarga sus datos.
    void irASeccion(int indice) {
        if (indice >= 0 && indice < kCantidadSecciones) {
            indiceSeccion_ = indice;
            recargarSeccionActual();
        }
    }

private:
    void moverSeleccion(int delta) {
        EstadoSeccion& estado = estadoActual();
        if (estado.items.empty()) return;
        const int tope = static_cast<int>(estado.items.size()) - 1;
        estado.seleccion = std::max(0, std::min(tope, estado.seleccion + delta));
    }

    // Lleva la selección al primer y al último ítem de la sección.
    void irAlInicio() {
        estadoActual().seleccion = 0;
    }

    void irAlFinal() {
        EstadoSeccion& estado = estadoActual();
        estado.seleccion = static_cast<int>(estado.items.size()) - 1;
        if (estado.seleccion < 0) estado.seleccion = 0;
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
            case SeccionTui::Chat:
                // El chat gestiona su propia entrada; Enter en una fila no
                // hace nada especial aquí.
                break;
        }
    }

    // ---- Caja de comandos ----

    // Teclas con el foco en la caja de comandos: Esc vuelve al panel, las
    // flechas recorren el historial y el resto lo atiende el propio input.
    bool eventoEnEntrada(ftxui::Event evento) {
        if (evento == ftxui::Event::CtrlC || evento == ftxui::Event::Character('\x03')) {
            // Interrumpir la TUI tiene que funcionar también con el foco en la
            // caja de comandos: si no, el carácter se escribiría en ella.
            pantalla_->Exit();
            return true;
        }
        if (evento == ftxui::Event::Escape) {
            desenfocarEntrada();
            return true;
        }
        if (evento == ftxui::Event::ArrowUp) {
            recorrerHistorial(-1);
            return true;
        }
        if (evento == ftxui::Event::ArrowDown) {
            recorrerHistorial(1);
            return true;
        }
        if (evento == ftxui::Event::Return) {
            // El comando se ejecuta aqui y no con on_enter del input: ese
            // callback lo difiere FTXUI al final del lote de eventos, y se
            // ejecutaria con el texto que hubiera al terminar el lote.
            ejecutarComandoEscrito();
            return true;
        }
        return entrada_->OnEvent(evento);
    }

    void enfocarEntrada() {
        focoEnEntrada_ = true;
        mensaje_ = "Escribe un comando y pulsa Enter (Esc para volver)";
        mensajeEsError_ = false;
    }

    void desenfocarEntrada() {
        focoEnEntrada_ = false;
    }

    // Traduce la línea escrita a una solicitud del núcleo y la ejecuta.
    void ejecutarComandoEscrito() {
        const std::string linea = textoComando_;
        const std::vector<Token> tokens = tokenizar(linea);
        if (tokens.empty()) return;

        nlohmann::json argumentos = nlohmann::json::object();
        std::string error;
        if (!construirArgumentos(tokens, argumentos, error)) {
            mensaje_ = error;
            mensajeEsError_ = true;
            return;
        }

        if (!linea.empty()) {
            historial_.push_back(linea);
            indiceHistorial_ = static_cast<int>(historial_.size());
        }

        const std::string comando = tokens[0].texto;
        const ResultadoComando resultado = llamar(comando, argumentos);
        if (resultado.respuesta.correcta) {
            mensaje_ = comando + ": ok" + resumenResultado(resultado.respuesta.resultado);
            mensajeEsError_ = false;
        } else {
            mensaje_ = comando + ": " + resultado.respuesta.codigoError + " - " +
                       resultado.respuesta.mensajeError;
            mensajeEsError_ = true;
        }
        textoComando_.clear();
        recargarSeccionActual(false);
    }

    // Resumen corto del resultado de un comando, para la barra de estado.
    static std::string resumenResultado(const nlohmann::json& resultado) {
        if (!resultado.is_object()) return {};
        if (resultado.contains("count") && resultado["count"].is_number_integer()) {
            return " (" + std::to_string(resultado["count"].get<long long>()) + ")";
        }
        if (resultado.contains("name") && resultado["name"].is_string()) {
            return " (" + resultado["name"].get<std::string>() + ")";
        }
        return {};
    }

    // Recorre el historial de comandos escritos.
    void recorrerHistorial(int delta) {
        if (historial_.empty()) return;
        if (indiceHistorial_ < 0) indiceHistorial_ = static_cast<int>(historial_.size());
        indiceHistorial_ = std::max(
            0, std::min(static_cast<int>(historial_.size()), indiceHistorial_ + delta));
        textoComando_ = indiceHistorial_ < static_cast<int>(historial_.size())
                            ? historial_[indiceHistorial_]
                            : std::string();
    }

    // ---- Chat de Silvy ----

    void enfocarChat() {
        focoEnChat_ = true;
        focoEnEntrada_ = false;
        mensaje_ = "Escribe una orden para Silvy y pulsa Enter (Esc para volver)";
        mensajeEsError_ = false;
    }

    void desenfocarChat() {
        focoEnChat_ = false;
    }

    // Teclas con el foco en la caja de chat de Silvy.
    bool eventoEnChat(ftxui::Event evento) {
        if (evento == ftxui::Event::CtrlC || evento == ftxui::Event::Character('\x03')) {
            pantalla_->Exit();
            return true;
        }
        if (evento == ftxui::Event::Escape) {
            desenfocarChat();
            return true;
        }
        if (evento == ftxui::Event::ArrowUp) {
            recorrerHistorialChat(-1);
            return true;
        }
        if (evento == ftxui::Event::ArrowDown) {
            recorrerHistorialChat(1);
            return true;
        }
        if (evento == ftxui::Event::PageUp) {
            chatDesplazamiento_ += filasVisibles();
            return true;
        }
        if (evento == ftxui::Event::PageDown) {
            chatDesplazamiento_ -= filasVisibles();
            return true;
        }
        if (evento == ftxui::Event::Return) {
            ejecutarOrdenChat();
            return true;
        }
        return entradaChat_->OnEvent(evento);
    }

    // Ejecuta la orden escrita en el chat a través de Silvy y la registra.
    void ejecutarOrdenChat() {
        const std::string linea = textoChat_;
        if (linea.empty()) return;

        ResultadoComando resultado = silvy_.ejecutarOrden(linea);

        std::string resumen = linea + " -> ";
        if (resultado.respuesta.correcta) {
            resumen += "ok";
        } else {
            resumen += "error " + resultado.respuesta.codigoError +
                       " - " + resultado.respuesta.mensajeError;
        }
        chatHistorial_.push_back(resumen);
        textoChat_.clear();
        chatIndiceHistorial_ = static_cast<int>(chatHistorial_.size());
        chatDesplazamiento_ = 0;  // mostrar las más recientes

        // Recargar la sección activa para reflejar posibles cambios en disco.
        recargarSeccionActual(false);
        mensaje_ = "Silvy: " + resumen;
        mensajeEsError_ = !resultado.respuesta.correcta;
    }

    // Recorre el historial de órdenes escritas en el chat.
    void recorrerHistorialChat(int delta) {
        if (chatHistorial_.empty()) return;
        if (chatIndiceHistorial_ < 0)
            chatIndiceHistorial_ = static_cast<int>(chatHistorial_.size());
        chatIndiceHistorial_ = std::max(
            0, std::min(static_cast<int>(chatHistorial_.size()),
                        chatIndiceHistorial_ + delta));
        textoChat_ = chatIndiceHistorial_ < static_cast<int>(chatHistorial_.size())
                         ? chatHistorial_[chatIndiceHistorial_]
                         : std::string();
    }

    // ---- Ratón ----

    // Traduce un clic a una acción sobre la lista de ítems, sobre los
    // dominios del panel lateral o sobre la caja de comandos.
    bool manejarRaton(ftxui::Event& evento) {
        const ftxui::Mouse& raton = evento.mouse();
        if (raton.button != ftxui::Mouse::Left || raton.motion != ftxui::Mouse::Pressed) {
            return false;
        }
        if (raton.y == filaCajaComandos()) {
            // Un clic en la caja de comandos la enfoca para poder escribir.
            enfocarEntrada();
            return true;
        }
        if (raton.x < kColumnaPanelCentral) {
            const int fila = raton.y - kFilaPrimeraSeccion;
            if (fila < 0 || fila >= kCantidadSecciones) return false;
            indiceSeccion_ = fila;
            recargarSeccionActual();
            mensaje_ = std::string("Seccion: ") + nombreSeccion(seccionActual());
            mensajeEsError_ = false;
            return true;
        }
        EstadoSeccion& estado = estadoActual();
        const int indice = estado.desplazamiento + (raton.y - kFilaPrimerItem);
        if (indice < 0 || indice >= static_cast<int>(estado.items.size())) return false;
        estado.seleccion = indice;
        return true;
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
                                                           std::max(20, anchoTerminal() - 34))) +
                               " ") |
                       ftxui::color(kTextoTenue),
               }) | ftxui::bgcolor(kFondoBarra);
    }

    ftxui::Element panelLateral() {
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

    ftxui::Element panelCentral() {
        if (ayudaVisible_) {
            return panelAyuda();
        }
        const SeccionTui seccion = seccionActual();
        if (seccion == SeccionTui::Chat) {
            return panelChat();
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

    // Panel con el catálogo de comandos, para consultar qué se puede pedir.
    ftxui::Element panelAyuda() {
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

    // ---- Panel de chat de Silvy ----

    // Renderiza la sección Chat: historial de auditoría de Silvy más la caja
    // de entrada para escribir órdenes en lenguaje natural o JSON.
    ftxui::Element panelChat() {
        const auto& entradas = auditoria_.entradas();
        const int visibles = filasVisibles();

        // Desplazamiento para que las entradas más recientes queden visibles.
        if (chatDesplazamiento_ < 0) chatDesplazamiento_ = 0;
        if (chatDesplazamiento_ > static_cast<int>(entradas.size()) - visibles &&
            static_cast<int>(entradas.size()) > visibles) {
            chatDesplazamiento_ = static_cast<int>(entradas.size()) - visibles;
        }

        std::vector<ftxui::Element> filas;
        filas.push_back(ftxui::hbox({
            ftxui::text(" Chat Silvy  (últimas órdenes ejecutadas)") | ftxui::bold,
            ftxui::filler(),
        }));
        filas.push_back(ftxui::separator());

        int inicio = static_cast<int>(entradas.size()) - visibles - chatDesplazamiento_;
        if (inicio < 0) inicio = 0;
        int fin = std::min(static_cast<int>(entradas.size()), inicio + visibles);

        for (int i = inicio; i < fin; ++i) {
            const EntradaAuditoria& e = entradas[i];
            std::string linea = "  [" + e.marcaTiempo.substr(11, 8) + "] " + e.comando;
            if (!e.argumentos.empty() && e.argumentos != nlohmann::json::object()) {
                linea += " args=" + e.argumentos.dump();
            }
            linea += " -> " + std::string(e.ok ? "ok" : ("error " + e.codigoError));
            if (!e.mensajeError.empty()) {
                linea += " (" + e.mensajeError + ")";
            }
            ftxui::Color color = e.ok ? kExito : kError;
            if (e.modificaDisco) {
                linea += " [DISCO]";
                color = kAdvertencia;
            }
            filas.push_back(ftxui::text(recortar(linea, anchoTerminal() - 4)) | ftxui::color(color));
        }

        if (entradas.empty()) {
            filas.push_back(ftxui::text("  (sin órdenes aún)") | ftxui::color(kTextoTenue));
        }

        filas.push_back(ftxui::separator());

        // Caja de entrada del chat.
        ftxui::Element entradaChat = entradaChat_->Render();
        filas.push_back(ftxui::hbox({
            ftxui::text(focoEnChat_ ? " > " : " / ") | ftxui::color(kAcento),
            std::move(entradaChat) | ftxui::flex,
        }) | ftxui::bgcolor(kFondoBarra));

        filas.push_back(ftxui::text(" ↑↓ historial · Enter ejecuta · Esc vuelve · / JSON directo ") |
                        ftxui::color(kTextoTenue) | ftxui::bgcolor(kFondoBarra));

        return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel);
    }

    // Caja de comandos: una línea de entrada donde se escriben las
    // solicitudes del catálogo.
    ftxui::Element cajaComandos() {
        ftxui::Element entrada = entrada_->Render();
        return ftxui::hbox({
                   ftxui::text(focoEnEntrada_ ? " > " : " / ") | ftxui::color(kAcento),
                   std::move(entrada) | ftxui::flex,
               }) | ftxui::bgcolor(kFondoBarra);
    }

    // ---- Miembros ----

    ftxui::ScreenInteractive* pantalla_ = nullptr;
    Auditoria& auditoria_;
    Harness& harness_;
    Silvy& silvy_;

    ftxui::Component entrada_;
    std::string textoComando_;
    std::vector<std::string> historial_;
    int indiceHistorial_ = 0;

    // Chat de Silvy integrado en la TUI.
    ftxui::Component entradaChat_;
    std::string textoChat_;
    std::vector<std::string> chatHistorial_;
    int chatIndiceHistorial_ = 0;
    int chatDesplazamiento_ = 0;
    bool focoEnChat_ = false;

    std::vector<EstadoSeccion> estados_ = std::vector<EstadoSeccion>(kCantidadSecciones);
    int indiceSeccion_ = 0;
    std::string proyectoActivo_;
    std::string mensaje_ = "Listo";
    bool mensajeEsError_ = false;
    bool focoEnEntrada_ = false;
    bool ayudaVisible_ = false;
};

}  // namespace

std::string errorDeEntradaTui() {
    if (clasificarEntradaTui() != EntradaTui::SinTerminal) {
        return {};
    }
    return "La TUI necesita una terminal interactiva y stdin no lo es "
           "(¿redirigido desde un archivo o una tubería, o lanzado por un "
           "programa sin terminal?). Ejecuta `FunshiCLI --tui` directamente en "
           "una terminal.";
}

int ejecutarTui(int seccionInicial) {
    const EntradaTui entrada = clasificarEntradaTui();
    if (entrada == EntradaTui::SinTerminal) {
        // El motivo ya lo informó quien invocó la TUI; aquí solo se devuelve
        // el código de salida.
        return codigosSalida::kErrorUso;
    }
    ftxui::ScreenInteractive pantalla = ftxui::ScreenInteractive::Fullscreen();
    // Las instancias de Agente viven durante todo el bucle de la TUI y
    // Silvy las usa para ejecutar órdenes desde la sección Chat.
    Auditoria auditoria;
    Harness harness(auditoria);
    Silvy silvy(harness);
    auto aplicacion =
        std::make_shared<AplicacionTui>(&pantalla, auditoria, harness, silvy);
    if (seccionInicial >= 0 && seccionInicial < kCantidadSecciones) {
        aplicacion->irASeccion(seccionInicial);
    }
    // En un pty sin consola FTXUI no lee teclas: se las entrega este lector,
    // que además detecta el tamaño de la terminal para el dibujo.
    EntradaRespaldo respaldo;
    if (entrada == EntradaTui::LecturaRespaldo) {
        respaldo.iniciar(&pantalla);
    }
    pantalla.Loop(aplicacion);
    return codigosSalida::kExito;
}

}  // namespace funshi
