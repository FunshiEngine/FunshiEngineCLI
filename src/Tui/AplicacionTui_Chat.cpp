/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — módulo chat
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


void AplicacionTui::enfocarChat() {
    focoEnChat_ = true;
    focoEnEntrada_ = false;
    mensaje_ = "Escribe una orden para Silvy y pulsa Enter (Esc para volver)";
    mensajeEsError_ = false;
}

void AplicacionTui::desenfocarChat() {
    focoEnChat_ = false;
}

bool AplicacionTui::eventoEnChat(ftxui::Event evento) {
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

void AplicacionTui::ejecutarOrdenChat() {
    const std::string linea = textoChat_;
    if (linea.empty()) return;

    auto respuesta = silvy_.procesar(linea);

    std::string resumen = linea + " -> ";
    if (respuesta.fueComandoCli) {
        if (respuesta.resultado.respuesta.correcta) {
            resumen += "ok";
        } else {
            resumen += "error " + respuesta.resultado.respuesta.codigoError +
                       " - " + respuesta.resultado.respuesta.mensajeError;
        }
    } else {
        resumen += respuesta.texto;
    }
    chatHistorial_.push_back(resumen);
    textoChat_.clear();
    chatIndiceHistorial_ = static_cast<int>(chatHistorial_.size());
    chatDesplazamiento_ = 0;

    // Recargar la sección activa para reflejar posibles cambios en disco.
    recargarSeccionActual(false);
    mensaje_ = "Silvy: " + resumen;
    mensajeEsError_ = respuesta.error;
}

void AplicacionTui::recorrerHistorialChat(int delta) {
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

ftxui::Element AplicacionTui::panelChat() {
    const auto& entradas = auditoria_.entradas();
    const int visibles = filasVisibles();

    // Desplazamiento para que las entradas más recientes queden visibles.
    if (chatDesplazamiento_ < 0) chatDesplazamiento_ = 0;
    if (chatDesplazamiento_ > static_cast<int>(entradas.size()) - visibles &&
        static_cast<int>(entradas.size()) > visibles) {
        chatDesplazamiento_ = static_cast<int>(entradas.size()) - visibles;
    }

    std::vector<ftxui::Element> filas;
    
    // Header con indicador de modo
    std::string modeloActivo = silvy_.modeloActivo();
    std::string header = " Chat Silvy";
    if (!modeloActivo.empty()) {
        header += "  [LOCAL: " + modeloActivo + "]";
    } else if (silvy_.tieneLlm()) {
        header += "  [REMOTE]";
    } else {
        header += "  [CLI]";
    }
    
    filas.push_back(ftxui::hbox({
        ftxui::text(header) | ftxui::bold,
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

}  // namespace funshi
