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
    switch (m.rol) {
        case MensajeChat::Rol::Usuario: prefijo = "Tú:    "; color = kAcento; bold = true; break;
        case MensajeChat::Rol::Silvy:
            prefijo = "Silvy: ";
            if (m.enCurso && texto.empty()) texto = "...";
            break;
        case MensajeChat::Rol::Herramienta: prefijo = "       "; color = kAdvertencia; break;
        case MensajeChat::Rol::Error: prefijo = "Error: "; color = kError; break;
        case MensajeChat::Rol::Estado:
        default: prefijo = "       "; color = kTextoTenue; tenue = true; break;
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
}

ftxui::Element AplicacionTui::panelChat() {
    metricas::Cronometro cronometro("tui.chat.frame");
    const auto tamTerminal = ftxui::Terminal::Size();
    const int alto = std::max(8, tamTerminal.dimy);
    const int ancho = std::max(30, tamTerminal.dimx - kColumnaPanelCentral - 6);
    const int visibles = std::max(4, alto - 10);

    // Solo se copian las líneas de la ventana visible; las demás quedan en la
    // caché de cada mensaje.
    std::vector<LineaChat> lineas;
    bool ocupado = false;
    int total = 0;
    int inicio = 0;
    int fin = 0;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        ocupado = chatOcupado_;
        for (auto& m : chatMensajes_) {
            if (m.versionCache != m.version || m.anchoCache != ancho) {
                partirMensajeChat(m, ancho, m.lineasCache);
                m.versionCache = m.version;
                m.anchoCache = ancho;
            }
            total += static_cast<int>(m.lineasCache.size());
        }
        int desplazamiento = std::max(0, chatDesplazamiento_);
        desplazamiento = std::min(desplazamiento, std::max(0, total - visibles));
        chatDesplazamiento_ = desplazamiento;
        inicio = std::max(0, total - visibles - desplazamiento);
        fin = std::min(total, inicio + visibles);

        int cursor = 0;
        for (const auto& m : chatMensajes_) {
            const int n = static_cast<int>(m.lineasCache.size());
            if (cursor + n > inicio && cursor < fin) {
                const int desde = std::max(0, inicio - cursor);
                const int hasta = std::min(n, fin - cursor);
                lineas.insert(lineas.end(), m.lineasCache.begin() + desde,
                              m.lineasCache.begin() + hasta);
            }
            cursor += n;
            if (cursor >= fin) break;
        }
    }

    if (total == 0) {
        lineas.push_back({"  Escribe una orden para Silvy. Usa /comando para ejecutar CLI directo.", kTextoTenue, false, true});
        lineas.push_back({"  Ejemplo: crear proyecto MiJuego 2d", kTextoTenue, false, true});
        lineas.push_back({"  Ejemplo: /project.list", kTextoTenue, false, true});
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
    if (ocupado) header += "  (procesando...)";

    filas.push_back(ftxui::hbox({
        ftxui::text(header) | ftxui::bold | ftxui::color(ocupado ? kAdvertencia : kAcento),
        ftxui::filler(),
        ftxui::text(total > visibles
                        ? " " + std::to_string(inicio + 1) + "-" + std::to_string(fin) +
                              "/" + std::to_string(total) + " "
                        : " ") |
            ftxui::color(kTextoTenue),
    }));
    filas.push_back(ftxui::separator());

    for (const auto& l : lineas) {
        auto elem = ftxui::text(l.texto) | ftxui::color(l.color);
        if (l.bold) elem = elem | ftxui::bold;
        if (l.tenue) elem = elem | ftxui::dim;
        filas.push_back(elem);
    }
    while ((int)filas.size() < visibles + 2) {
        filas.push_back(ftxui::text(" "));
    }

    filas.push_back(ftxui::separator());

    ftxui::Element entradaChat = entradaChat_->Render();
    filas.push_back(ftxui::hbox({
        ftxui::text(focoEnChat_ ? " > " : " / ") | ftxui::color(kAcento),
        std::move(entradaChat) | ftxui::flex,
    }) | ftxui::bgcolor(kFondoBarra));

    filas.push_back(ftxui::text(" ↑↓ historial · PgUp/PgDn scroll · Enter envía · Esc vuelve · /comando = CLI directo ") |
                    ftxui::color(kTextoTenue) | ftxui::bgcolor(kFondoBarra));

    return ftxui::vbox(std::move(filas)) | ftxui::flex | ftxui::bgcolor(kFondoPanel);
}

}  // namespace funshi
