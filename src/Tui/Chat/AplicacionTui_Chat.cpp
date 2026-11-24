/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — Chat Silvy: foco, mensajes, eventos y envío de órdenes.
// Cada orden se procesa en un hilo con la sesión en la que se escribió, así
// el usuario puede cambiar de sesión (o escribir en otra) mientras tanto.
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/TemaTui.h"
#include "Tui/Terminal/UtilTui.h"

#include "Agente/Silvy.h"
#include "Tui/Chat/MonitorChat.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace funshi {

void AplicacionTui::enfocarChat() {
    focoEnChat_ = true;
    focoEnEntrada_ = false;
    const auto s = sesionActual();
    mensaje_ = s && s->ocupado ? "Silvy está procesando en esta sesión (Ctrl+X cancela)"
                               : "Escribe una orden para Silvy · Ctrl+N nueva sesión · Ctrl+←/→ cambia";
    mensajeEsError_ = false;
}

void AplicacionTui::desenfocarChat() {
    focoEnChat_ = false;
}

int AplicacionTui::chatLineasVisibles() const {
    int alto = std::max(8, ftxui::Terminal::Size().dimy);
    // Barras, separadores, cabecera, barra de sesiones, entrada y ayuda.
    return std::max(4, alto - 11);
}

void AplicacionTui::agregarMensajeChat(SesionTui& sesion, const std::string& texto, MensajeChat::Rol rol,
                                       bool enCurso) {
    std::lock_guard<std::mutex> lock(chatMutex_);
    MensajeChat m;
    m.rol = rol;
    m.texto = texto;
    m.enCurso = enCurso;
    sesion.mensajes.push_back(std::move(m));
    sesion.desplazamiento = 0;
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
        if (!salirDeVistaSubagente()) desenfocarChat();
        return true;
    }
    if (evento == ftxui::Event::CtrlN) {
        ordenDeSesion("/nueva");
        return true;
    }
    if (evento == ftxui::Event::ArrowLeftCtrl || evento == ftxui::Event::CtrlP) {
        cambiarSesion(-1);
        return true;
    }
    if (evento == ftxui::Event::ArrowRightCtrl || evento == ftxui::Event::CtrlO) {
        cambiarSesion(1);
        return true;
    }
    if (evento == ftxui::Event::CtrlX) {
        ordenDeSesion("/cancelar");
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
    if (evento == ftxui::Event::PageUp || evento == ftxui::Event::PageDown) {
        desplazarChat((evento == ftxui::Event::PageUp ? 1 : -1) * std::max(1, chatLineasVisibles() - 2));
        return true;
    }
    if (evento == ftxui::Event::ArrowUpCtrl || evento == ftxui::Event::ArrowDownCtrl) {
        desplazarChat(evento == ftxui::Event::ArrowUpCtrl ? 3 : -3);
        return true;
    }
    if (evento == ftxui::Event::Return) {
        ejecutarOrdenChat();
        return true;
    }
    return entradaChat_->OnEvent(evento);
}

void AplicacionTui::procesarOrdenChat(std::shared_ptr<SesionTui> sesion, const std::string& linea) {
    // El monitor ordena los eventos en la sesión: razonamiento, herramientas
    // con argumentos y resultado, notas de progreso y un resumen por
    // subagente (cuya transcripción queda aparte, ver /sub N).
    chat::MonitorSesion monitor(*sesion);
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        monitor.esperar();
    }
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());

    auto onEvent = [&](const agente::llm::AgentEvent& evento) {
        using Tipo = agente::llm::AgentEvent::Type;
        {
            std::lock_guard<std::mutex> lock(chatMutex_);
            monitor.aplicar(evento);
        }
        // Los fragmentos de texto llegan muy seguido: redibujo limitado.
        const bool fragmento = evento.type == Tipo::Text || evento.type == Tipo::Pensamiento;
        solicitarRedibujoChat(!fragmento);
    };

    Silvy::RespuestaProcesada respuesta = silvy_.procesar(linea, sesion->datos.agente, onEvent);

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
    }

    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        monitor.cerrar(textoFinal, respuesta.error);
        sesion->ocupado = false;
        chatAplicarResultadoPendiente_ = true;
        chatMensajePendiente_ = "Silvy (" + sesion->datos.titulo + "): " +
                                (respuesta.error ? "error al procesar la orden" : "orden procesada");
        chatMensajePendienteError_ = respuesta.error;
        chatRecargarPendiente_ = true;
    }
    guardarSesion(sesion);
    if (pantalla_) pantalla_->PostEvent(ftxui::Event::Custom);
}

void AplicacionTui::ejecutarOrdenChat() {
    std::string linea;
    std::shared_ptr<SesionTui> sesion;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        linea = textoChat_;
        sesion = sesionActualSinLock();
    }
    if (linea.empty() || !sesion) return;
    salirDeVistaSubagente();  // la orden y su respuesta se ven en la conversación
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        chatHistorialEntrada_.push_back(linea);
        chatIndiceHistorial_ = static_cast<int>(chatHistorialEntrada_.size());
        textoChat_.clear();
    }
    if (ordenDeSesion(linea)) return;

    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        if (sesion->ocupado) {
            textoChat_ = linea;  // no se pierde lo escrito
            mensaje_ = "Esta sesión está procesando. Espera, cancela con Ctrl+X o abre otra con Ctrl+N.";
            mensajeEsError_ = true;
            return;
        }
        sesion->ocupado = true;
        sesion->datos.agente.proyectoActivo = proyectoActivo_;
        // La primera orden le da nombre a una sesión sin título propio.
        const bool sinTitulo = sesion->datos.titulo.rfind("Sesión ", 0) == 0;
        const bool primera = std::none_of(sesion->mensajes.begin(), sesion->mensajes.end(),
                                          [](const MensajeChat& m) { return m.rol == MensajeChat::Rol::Usuario; });
        if (sinTitulo && primera) sesion->datos.titulo = agente::SesionChat::tituloDesde(linea);
        MensajeChat usuario;
        usuario.rol = MensajeChat::Rol::Usuario;
        usuario.texto = linea;
        sesion->mensajes.push_back(std::move(usuario));
        sesion->desplazamiento = 0;
    }
    mensaje_ = "Silvy: procesando...";
    mensajeEsError_ = false;
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    std::thread([this, sesion, linea]() { procesarOrdenChat(sesion, linea); }).detach();
}

// Chat sin foco: flechas, páginas e Inicio/Fin recorren la conversación.
bool AplicacionTui::desplazarChatConTecla(const ftxui::Event& evento) {
    const int pagina = std::max(1, chatLineasVisibles() - 2);
    if (evento == ftxui::Event::ArrowUp) desplazarChat(1);
    else if (evento == ftxui::Event::ArrowDown) desplazarChat(-1);
    else if (evento == ftxui::Event::PageUp) desplazarChat(pagina);
    else if (evento == ftxui::Event::PageDown) desplazarChat(-pagina);
    else if (evento == ftxui::Event::Home) desplazarChat(kChatInicio);
    else if (evento == ftxui::Event::End) desplazarChat(kChatFinal);
    else return false;
    return true;
}

void AplicacionTui::recorrerHistorialChat(int delta) {
    std::lock_guard<std::mutex> lock(chatMutex_);
    if (chatHistorialEntrada_.empty()) return;
    if (chatIndiceHistorial_ < 0) chatIndiceHistorial_ = static_cast<int>(chatHistorialEntrada_.size());
    chatIndiceHistorial_ = std::max(0, std::min(static_cast<int>(chatHistorialEntrada_.size()),
                                                chatIndiceHistorial_ + delta));
    textoChat_ = chatIndiceHistorial_ < static_cast<int>(chatHistorialEntrada_.size())
                     ? chatHistorialEntrada_[chatIndiceHistorial_]
                     : std::string();
}

}  // namespace funshi
