/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — Chat Silvy: partido de líneas y dibujo del panel.
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/TemaTui.h"
#include "Tui/Terminal/UtilTui.h"
#include "Util/Metricas.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef FUNSHI_VERSION
#define FUNSHI_VERSION "0.0.0"
#endif

namespace funshi {


void AplicacionTui::partirMensajeChat(const MensajeChat& m, int ancho,
                                      std::vector<LineaChat>& salida) {
    salida.clear();
    std::string prefijo;
    ftxui::Color color = kTexto;
    bool bold = false;
    bool tenue = false;
    std::string texto = m.texto;
    if (m.expandido && !m.detalle.empty()) {
        // Expandido: la primera línea (estado y llamada) + el detalle completo.
        texto = texto.substr(0, texto.find('\n')) + "\n" + m.detalle;
    }
    using Rol = MensajeChat::Rol;
    switch (m.rol) {
        case Rol::Usuario:
            // "aclaracion": lo que el usuario agregó a un razonamiento en curso.
            prefijo = m.ref == "aclaracion" ? "Tú +   " : "Tú:    ";
            color = kAcento;
            bold = true;
            break;
        case Rol::Silvy:
            prefijo = "Silvy: ";
            if (m.enCurso && texto.empty()) texto = "...";
            break;
        case Rol::Pensamiento: prefijo = "Piensa "; color = kTextoTenue; tenue = true; break;
        case Rol::Herramienta:
            prefijo = "Acción ";
            color = m.enCurso ? kAdvertencia : (m.ok ? kExito : kError);
            break;
        case Rol::Subagente:
            prefijo = "Subag. ";
            color = m.enCurso ? kAdvertencia : (m.ok ? kAcento : kError);
            bold = true;
            break;
        case Rol::Tarea: prefijo = "Tarea: "; color = kAcento; bold = true; break;
        case Rol::Codigo: prefijo = "       "; color = kTexto; break;
        case Rol::Error: prefijo = "Error: "; color = kError; break;
        case Rol::Estado: prefijo = "  ·    "; color = kTextoTenue; tenue = true; break;
    }
    // Sangría de continuación con el ancho visible del prefijo (no sus bytes:
    // "Tú" ocupa 2 columnas pero 3 bytes).
    const std::size_t anchoPrefijo = static_cast<std::size_t>(ftxui::string_width(prefijo));
    const std::string sangria(anchoPrefijo, ' ');
    const std::size_t anchoUtil =
        static_cast<std::size_t>(std::max<int>(8, ancho - static_cast<int>(anchoPrefijo)));

    if (texto.empty()) {
        salida.push_back({prefijo, color, bold, tenue});
        return;
    }
    bool primera = true;
    std::size_t inicio = 0;
    while (inicio <= texto.size()) {
        std::size_t fin = texto.find('\n', inicio);
        if (fin == std::string::npos) fin = texto.size();
        std::string resto = texto.substr(inicio, fin - inicio);
        if (!resto.empty() && resto.back() == '\r') resto.pop_back();
        // Partir por columnas visibles sin cortar caracteres UTF-8.
        do {
            const std::size_t corte = resto.empty() ? 0 : std::max<std::size_t>(1, byteDeAncho(resto, anchoUtil));
            std::string trozo = resto.substr(0, corte);
            resto.erase(0, corte);
            salida.push_back({(primera ? prefijo : sangria) + (trozo.empty() ? " " : trozo),
                              color, bold, tenue});
            primera = false;
        } while (!resto.empty());
        if (fin == texto.size()) break;
        inicio = fin + 1;
    }
    // El razonamiento se muestra plegado: sus primeras líneas y un clic para
    // verlo entero (o volver a plegarlo).
    if (m.rol == Rol::Pensamiento && salida.size() > kLineasPensamientoPlegado) {
        if (!m.expandido) {
            const std::size_t ocultas = salida.size() - kLineasPensamientoPlegado;
            salida.resize(kLineasPensamientoPlegado);
            salida.push_back({sangria + "… " + std::to_string(ocultas) + (ocultas == 1 ? " línea más" : " líneas más") +
                                  " · clic para ver el razonamiento",
                              kAcento, false, true});
        } else {
            salida.push_back({sangria + "(clic para plegar el razonamiento)", kAcento, false, true});
        }
    }
}

ftxui::Element AplicacionTui::panelChat() {
    metricas::Cronometro cronometro("tui.chat.frame");
    const auto tamTerminal = ftxui::Terminal::Size();
    const int alto = std::max(8, tamTerminal.dimy);
    const int ancho = std::max(30, tamTerminal.dimx - kColumnaPanelCentral - 6);
    const int visibles = std::max(4, alto - 11);  // una fila más: barra de sesiones

    // Solo se copian las líneas de la ventana visible; las demás quedan en la
    // caché de cada mensaje.
    std::vector<LineaChat> lineas;
    std::vector<std::string> refs;  // a qué se refiere cada línea (para el clic)
    std::string cabeceraSub;        // vista de subagente: su título y estado
    bool ocupado = false;
    std::string opciones;     // elección pendiente (mensaje en espera)
    bool desplazado = false;  // mirando más arriba del final
    int total = 0;
    int inicio = 0;
    int fin = 0;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        const auto sesion = sesionActualSinLock();
        static std::vector<MensajeChat> sinMensajes;
        const auto sub = sesion && !sesion->vista.empty() ? sesion->subagente(sesion->vista) : nullptr;
        if (sesion && !sesion->vista.empty() && !sub) sesion->vista.clear();
        auto& mensajes = sub ? sub->mensajes : (sesion ? sesion->mensajes : sinMensajes);
        int sinDesplazamiento = 0;
        int& desplazamientoSesion =
            sub ? sub->desplazamiento : (sesion ? sesion->desplazamiento : sinDesplazamiento);
        if (sub) {
            cabeceraSub = " Subagente " + std::to_string(sub->numero) + " · " + sub->datos.titulo + " · " +
                          sub->datos.estado;
        }
        ocupado = sesion && sesion->ocupado;
        if (sesion && !sesion->enEspera.empty()) {
            opciones = " ⏸ «" + recortar(sesion->enEspera, 40) +
                       "»  A: agregar a lo que está pensando · E: encolar · Esc: seguir editando ";
        }
        for (auto& m : mensajes) {
            if (m.versionCache != m.version || m.anchoCache != ancho) {
                partirMensajeChat(m, ancho, m.lineasCache);
                m.versionCache = m.version;
                m.anchoCache = ancho;
            }
            total += static_cast<int>(m.lineasCache.size());
        }
        // Si el usuario está mirando más arriba, las líneas nuevas no le
        // mueven la vista: el desplazamiento (medido desde el final) crece con ellas.
        int& dibujadas = sub ? sub->lineasDibujadas : (sesion ? sesion->lineasDibujadas : sinDesplazamiento);
        if (desplazamientoSesion > 0 && dibujadas >= 0 && total > dibujadas) {
            desplazamientoSesion += total - dibujadas;
        }
        dibujadas = total;
        int desplazamiento = std::max(0, desplazamientoSesion);
        desplazamiento = std::min(desplazamiento, std::max(0, total - visibles));
        desplazamientoSesion = desplazamiento;
        desplazado = desplazamiento > 0;
        inicio = std::max(0, total - visibles - desplazamiento);
        fin = std::min(total, inicio + visibles);

        int cursor = 0;
        for (std::size_t i = 0; i < mensajes.size(); ++i) {
            const auto& m = mensajes[i];
            const int n = static_cast<int>(m.lineasCache.size());
            if (cursor + n > inicio && cursor < fin) {
                const int desde = std::max(0, inicio - cursor);
                const int hasta = std::min(n, fin - cursor);
                lineas.insert(lineas.end(), m.lineasCache.begin() + desde,
                              m.lineasCache.begin() + hasta);
                std::string ref;
                if (m.rol == MensajeChat::Rol::Subagente && !m.ref.empty()) ref = "sub:" + m.ref;
                else if (!m.detalle.empty() || m.rol == MensajeChat::Rol::Pensamiento) ref = "msg:" + std::to_string(i);
                refs.insert(refs.end(), static_cast<std::size_t>(hasta - desde), ref);
            }
            cursor += n;
            if (cursor >= fin) break;
        }
    }

    if (total == 0) {
        lineas.push_back({"  Escribe una orden para Silvy. Usa /comando para ejecutar CLI directo.", kTextoTenue, false, true});
        lineas.push_back({"  Ejemplo: crea un cubo con física en MiJuego", kTextoTenue, false, true});
        lineas.push_back({"  Ejemplo: /project.list", kTextoTenue, false, true});
        lineas.push_back({"  /ayuda muestra las órdenes de sesión (/nueva, /sesiones, /sesion N…)", kTextoTenue, false, true});
        inicio = 0;
        fin = static_cast<int>(lineas.size());
    }

    std::vector<ftxui::Element> filas;

    std::string modeloActivo = silvy_.modeloActivo();
    std::string header = " Chat Silvy";
    if (!modeloActivo.empty()) {
        header += "  [LOCAL: " + modeloActivo + "]";
    } else if (silvy_.tieneLlm()) {
        header += "  [REMOTE]";
    } else {
        header += "  [CLI]";
    }
    if (ocupado) header += "  (procesando... Ctrl+X cancela)";
    if (!cabeceraSub.empty()) header = cabeceraSub + "   (Esc vuelve a la conversación)";
    if (desplazado) header += "  ↓ más abajo: AvPág, rueda o Fin";

    filas.push_back(ftxui::hbox({
        ftxui::text(header) | ftxui::bold | ftxui::color(ocupado ? kAdvertencia : kAcento),
        ftxui::filler(),
        ftxui::text(total > visibles
                        ? " " + std::to_string(inicio + 1) + "-" + std::to_string(fin) +
                              "/" + std::to_string(total) + " "
                        : " ") |
            ftxui::color(kTextoTenue),
    }));
    filas.push_back(barraSesiones(ancho + 4));
    filas.push_back(ftxui::separator());

    // Cada línea guarda su caja en pantalla para saber qué se cliqueó.
    refs.resize(lineas.size());
    cajasChat_.assign(lineas.size(), ftxui::Box{});
    refsChat_ = refs;
    for (std::size_t i = 0; i < lineas.size(); ++i) {
        const auto& l = lineas[i];
        auto elem = ftxui::text(l.texto) | ftxui::color(l.color);
        if (l.bold) elem = elem | ftxui::bold;
        if (l.tenue) elem = elem | ftxui::dim;
        if (!refs[i].empty()) elem = elem | ftxui::reflect(cajasChat_[i]);
        filas.push_back(elem);
    }
    while ((int)filas.size() < visibles + 2) {
        filas.push_back(ftxui::text(" "));
    }

    filas.push_back(ftxui::separator());
    if (!opciones.empty()) {
        filas.push_back(ftxui::text(opciones) | ftxui::bold | ftxui::color(kFondoPanel) | ftxui::bgcolor(kAdvertencia));
    }

    ftxui::Element entradaChat = entradaChat_->Render();
    filas.push_back(ftxui::hbox({
        ftxui::text(focoEnChat_ ? " > " : " / ") | ftxui::color(kAcento),
        std::move(entradaChat) | ftxui::flex,
    }) | ftxui::bgcolor(kFondoBarra));

    filas.push_back(ftxui::text(cabeceraSub.empty()
                                    ? " ↑↓ historial · RePág/AvPág, Ctrl+↑↓ o rueda: desplazar · clic: detalle/subagente · /ayuda "
                                    : " PgUp/PgDn scroll · clic en una acción: detalle · Esc o /volver: conversación ") |
                    ftxui::color(kTextoTenue) | ftxui::bgcolor(kFondoBarra));

    return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel);
}

}  // namespace funshi
