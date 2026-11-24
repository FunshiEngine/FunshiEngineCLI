/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — vista de subagentes y clic en el chat.
//
// Cada subagente que lanza Silvy deja en la conversación una línea de
// resumen. Con un clic en ella (o /sub N) el panel del chat pasa a mostrar la
// transcripción de ese subagente: su tarea, su razonamiento, sus herramientas
// y su informe, en vivo si sigue trabajando. Esc o /volver regresa.
#include "Tui/AplicacionTui.h"

#include <algorithm>
#include <cstdlib>

namespace funshi {

void AplicacionTui::abrirSubagente(const std::string& id) {
    std::string titulo;
    int numero = 0;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        auto sesion = sesionActualSinLock();
        if (!sesion) return;
        auto sub = sesion->subagente(id);
        if (!sub) return;
        sesion->vista = id;
        sub->desplazamiento = 0;
        titulo = sub->datos.titulo;
        numero = sub->numero;
    }
    mensaje_ = "Subagente " + std::to_string(numero) + ": " + titulo + " · Esc vuelve a la conversación";
    mensajeEsError_ = false;
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
}

bool AplicacionTui::salirDeVistaSubagente() {
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        auto sesion = sesionActualSinLock();
        if (!sesion || sesion->vista.empty()) return false;
        sesion->vista.clear();
    }
    mensaje_ = "Conversación de la sesión";
    mensajeEsError_ = false;
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    return true;
}

bool AplicacionTui::ordenDeSubagentes(const std::string& orden, const std::string& resto) {
    if (orden == "docs") return ordenDeDocumentos(orden, resto);
    if (orden != "subs" && orden != "sub" && orden != "volver") return false;
    if (orden == "volver") {
        if (!salirDeVistaSubagente()) agregarMensajeChat(*sesionActual(), "Ya estás en la conversación.", MensajeChat::Rol::Estado);
        return true;
    }
    auto sesion = sesionActual();
    if (!sesion) return true;
    std::string lista;
    std::string id;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        const int n = std::atoi(resto.c_str());
        if (orden == "sub" && n >= 1 && n <= static_cast<int>(sesion->subagentes.size())) {
            id = sesion->subagentes[static_cast<std::size_t>(n - 1)]->datos.id;
        }
        for (const auto& s : sesion->subagentes) {
            lista += "\n  " + std::to_string(s->numero) + "  " + s->datos.titulo + " · " + s->datos.estado +
                     " · perfil " + s->datos.perfil;
        }
    }
    if (!id.empty()) {
        abrirSubagente(id);
        return true;
    }
    salirDeVistaSubagente();
    if (lista.empty()) {
        agregarMensajeChat(*sesion, "Esta sesión todavía no lanzó subagentes.", MensajeChat::Rol::Estado);
    } else {
        agregarMensajeChat(*sesion,
                           (orden == "sub" ? "Uso: /sub N. " : std::string()) + "Subagentes de esta sesión:" + lista +
                               "\n/sub N para ver uno (o clic en su línea).",
                           orden == "sub" ? MensajeChat::Rol::Error : MensajeChat::Rol::Estado);
    }
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    return true;
}

void AplicacionTui::desplazarChat(int lineas) {
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        auto s = sesionActualSinLock();
        if (!s) return;
        auto sub = s->vista.empty() ? nullptr : s->subagente(s->vista);
        int& desplazamiento = sub ? sub->desplazamiento : s->desplazamiento;
        // El tope superior lo ajusta el panel al dibujar (conoce el total).
        if (lineas == kChatFinal) desplazamiento = 0;
        else desplazamiento = std::max(0, std::min(kChatInicio, desplazamiento + lineas));
    }
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
}

// Clic simple sobre el chat: abre un subagente o expande/contrae una
// herramienta. Devuelve true si el clic cayó sobre algo accionable.
bool AplicacionTui::clicEnChat(int x, int y) {
    std::string ref;
    for (std::size_t i = 0; i < cajasChat_.size() && i < refsChat_.size(); ++i) {
        const ftxui::Box& c = cajasChat_[i];
        if (x >= c.x_min && x <= c.x_max && y >= c.y_min && y <= c.y_max) {
            ref = refsChat_[i];
            break;
        }
    }
    if (ref.rfind("sub:", 0) == 0) {
        abrirSubagente(ref.substr(4));
        return true;
    }
    if (ref.rfind("msg:", 0) == 0) {
        const std::size_t indice = static_cast<std::size_t>(std::atol(ref.c_str() + 4));
        {
            std::lock_guard<std::mutex> lock(chatMutex_);
            auto sesion = sesionActualSinLock();
            if (!sesion) return false;
            auto sub = sesion->vista.empty() ? nullptr : sesion->subagente(sesion->vista);
            auto& mensajes = sub ? sub->mensajes : sesion->mensajes;
            if (indice >= mensajes.size()) return false;
            mensajes[indice].expandido = !mensajes[indice].expandido;
            mensajes[indice].tocar();
        }
        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
        return true;
    }
    return false;
}

}  // namespace funshi
