/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — Chat Silvy: foco, mensajes, eventos y envío de órdenes.
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

namespace {

std::string resumenHerramientaChat(const std::string& nombre, bool ok) {
    return std::string(ok ? "[ok] " : "[error] ") + nombre;
}

}  // namespace

void AplicacionTui::enfocarChat() {
    focoEnChat_ = true;
    focoEnEntrada_ = false;
    if (chatOcupado_) {
        mensaje_ = "Silvy está procesando una orden...";
        mensajeEsError_ = false;
    } else {
        mensaje_ = "Escribe una orden para Silvy y pulsa Enter (Esc para volver)";
        mensajeEsError_ = false;
    }
}

void AplicacionTui::desenfocarChat() {
    focoEnChat_ = false;
}

int AplicacionTui::chatLineasVisibles() const {
    int alto = std::max(8, ftxui::Terminal::Size().dimy);
    // barra superior, separador, footer separador, mensaje, atajos, header/separador del chat,
    // separador, input y ayuda = margen conservador.
    return std::max(4, alto - 10);
}

void AplicacionTui::agregarMensajeChat(const std::string& texto, int rol, bool enCurso) {
    std::lock_guard<std::mutex> lock(chatMutex_);
    MensajeChat m;
    m.rol = static_cast<MensajeChat::Rol>(rol);
    m.texto = texto;
    m.enCurso = enCurso;
    chatMensajes_.push_back(std::move(m));
    chatDesplazamiento_ = 0;
}

void AplicacionTui::anexarTextoMensajeChat(std::size_t indice, const std::string& texto) {
    std::lock_guard<std::mutex> lock(chatMutex_);
    if (indice >= chatMensajes_.size()) return;
    chatMensajes_[indice].texto += texto;
    ++chatMensajes_[indice].version;
}

void AplicacionTui::finalizarMensajeChat(std::size_t indice, const std::string& textoFinal, bool error) {
    std::lock_guard<std::mutex> lock(chatMutex_);
    if (indice >= chatMensajes_.size()) return;
    chatMensajes_[indice].texto = textoFinal;
    chatMensajes_[indice].enCurso = false;
    if (error) chatMensajes_[indice].rol = MensajeChat::Rol::Error;
    ++chatMensajes_[indice].version;
}

void AplicacionTui::solicitarRedibujoChat(bool forzar) {
    if (!pantalla_) return;
    const long long ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
    long long ultimo = ultimoRedibujoChatMs_.load();
    if (!forzar && ahora - ultimo < 33) return;  // ya hay un frame reciente
    if (ultimoRedibujoChatMs_.compare_exchange_strong(ultimo, ahora) || forzar) {
        pantalla_->PostEvent(ftxui::Event());
    }
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
        chatDesplazamiento_ += chatLineasVisibles();
        return true;
    }
    if (evento == ftxui::Event::PageDown) {
        chatDesplazamiento_ -= chatLineasVisibles();
        if (chatDesplazamiento_ < 0) chatDesplazamiento_ = 0;
        return true;
    }
    if (evento == ftxui::Event::Return) {
        ejecutarOrdenChat();
        return true;
    }
    return entradaChat_->OnEvent(evento);
}

void AplicacionTui::procesarOrdenChat(const std::string& linea) {
    auto post = [this]() {
        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    };

    std::size_t indiceRespuesta = 0;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        MensajeChat respuesta;
        respuesta.rol = MensajeChat::Rol::Silvy;
        respuesta.texto = "";
        respuesta.enCurso = true;
        chatMensajes_.push_back(std::move(respuesta));
        indiceRespuesta = chatMensajes_.size() - 1;
        chatDesplazamiento_ = 0;
    }
    post();

    std::string textoAcumulado;
    auto onEvent = [&](const agente::llm::AgentEvent& evento) {
        using Tipo = agente::llm::AgentEvent::Type;
        if (evento.type == Tipo::Text) {
            if (!evento.delta.empty()) {
                textoAcumulado += evento.delta;
                anexarTextoMensajeChat(indiceRespuesta, evento.delta);
                solicitarRedibujoChat();
            }
            return;
        }
        if (evento.type == Tipo::ToolStart) {
            agregarMensajeChat(std::string("[tool] ") + evento.name, 2, false);
            post();
            return;
        }
        if (evento.type == Tipo::ToolEnd) {
            std::string resumen = resumenHerramientaChat(evento.name, evento.ok);
            if (!evento.result.empty()) {
                std::string r = evento.result;
                if (r.size() > 180) r = r.substr(0, 177) + "...";
                for (char& c : r) if (c == '\n') c = ' ';
                resumen += " - " + r;
            }
            agregarMensajeChat(resumen, 2, false);
            post();
            return;
        }
    };

    Silvy::RespuestaProcesada respuesta = silvy_.procesar(linea, onEvent);

    std::string textoFinal = respuesta.texto;
    if (respuesta.fueComandoCli) {
        if (respuesta.resultado.respuesta.correcta) {
            std::string cuerpo = respuesta.resultado.respuesta.resultado.dump(2);
            if (cuerpo == "null" || cuerpo == "{}" || cuerpo == "[]") cuerpo.clear();
            textoFinal = "Comando ejecutado: " + linea;
            if (!cuerpo.empty()) textoFinal += "\n" + cuerpo;
        } else {
            textoFinal = "Error: " + respuesta.resultado.respuesta.codigoError;
            if (!respuesta.resultado.respuesta.mensajeError.empty()) {
                textoFinal += " - " + respuesta.resultado.respuesta.mensajeError;
            }
        }
    } else if (textoFinal.empty() && !textoAcumulado.empty()) {
        textoFinal = textoAcumulado;
    } else if (textoFinal.empty()) {
        textoFinal = "(sin respuesta)";
    }

    finalizarMensajeChat(indiceRespuesta, textoFinal, respuesta.error);
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        chatOcupado_ = false;
        chatAplicarResultadoPendiente_ = true;
        chatMensajePendiente_ = respuesta.error ? "Silvy: error al procesar la orden"
                                                : "Silvy: orden procesada";
        chatMensajePendienteError_ = respuesta.error;
        chatRecargarPendiente_ = true;
    }
    if (pantalla_) pantalla_->PostEvent(ftxui::Event::Custom);
}

void AplicacionTui::ejecutarOrdenChat() {
    std::string linea;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        if (chatOcupado_) {
            mensaje_ = "Silvy está procesando una orden. Espera a que termine.";
            mensajeEsError_ = true;
            return;
        }
        linea = textoChat_;
    }
    if (linea.empty()) return;

    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        chatHistorialEntrada_.push_back(linea);
        chatIndiceHistorial_ = static_cast<int>(chatHistorialEntrada_.size());
        textoChat_.clear();
        chatOcupado_ = true;
        MensajeChat usuario;
        usuario.rol = MensajeChat::Rol::Usuario;
        usuario.texto = linea;
        chatMensajes_.push_back(std::move(usuario));
        chatDesplazamiento_ = 0;
    }

    mensaje_ = "Silvy: procesando...";
    mensajeEsError_ = false;
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());

    std::thread([this, linea]() { procesarOrdenChat(linea); }).detach();
}

void AplicacionTui::recorrerHistorialChat(int delta) {
    std::lock_guard<std::mutex> lock(chatMutex_);
    if (chatHistorialEntrada_.empty()) return;
    if (chatIndiceHistorial_ < 0)
        chatIndiceHistorial_ = static_cast<int>(chatHistorialEntrada_.size());
    chatIndiceHistorial_ = std::max(
        0, std::min(static_cast<int>(chatHistorialEntrada_.size()),
                    chatIndiceHistorial_ + delta));
    textoChat_ = chatIndiceHistorial_ < static_cast<int>(chatHistorialEntrada_.size())
                     ? chatHistorialEntrada_[chatIndiceHistorial_]
                     : std::string();
}
}  // namespace funshi
