/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — sesiones del chat: carga, guardado, órdenes de sesión y
// barra de pestañas. Las órdenes sin punto (/nueva, /sesion 2…) son del chat;
// las que llevan punto (/project.list) siguen yendo a la CLI.
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/TemaTui.h"
#include "Tui/Terminal/UtilTui.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>

namespace funshi {

namespace {

const char* kAyudaSesiones =
    "Sesiones del chat:\n"
    "  /nueva [título]     abre una sesión nueva (Ctrl+N)\n"
    "  /sesiones           lista las sesiones\n"
    "  /sesion N           cambia a la sesión N (Ctrl+← / Ctrl+→)\n"
    "  /renombrar título   cambia el título de esta sesión\n"
    "  /limpiar            vacía la conversación de esta sesión\n"
    "  /borrar             borra esta sesión (pide confirmación)\n"
    "  /cancelar           corta la respuesta en curso (Ctrl+X)\n"
    "Subagentes:\n"
    "  /subs               lista los subagentes de esta sesión\n"
    "  /sub N              muestra la tarea y el trabajo del subagente N (o clic en su línea)\n"
    "  /volver             regresa a la conversación (Esc)\n"
    "En la conversación se ven el razonamiento del modelo, cada herramienta con sus\n"
    "argumentos y su resultado (clic en una herramienta lo muestra completo) y notas de progreso.\n"
    "Cada sesión recuerda su conversación y se guarda sola. Varias pueden trabajar a la vez.";

}  // namespace

std::shared_ptr<AplicacionTui::SesionTui> AplicacionTui::sesionActualSinLock() const {
    if (sesiones_.empty()) return nullptr;
    return sesiones_[std::min(sesionActiva_, sesiones_.size() - 1)];
}

std::shared_ptr<AplicacionTui::SesionTui> AplicacionTui::sesionActual() const {
    std::lock_guard<std::mutex> lock(chatMutex_);
    return sesionActualSinLock();
}

void AplicacionTui::cargarSesiones() {
    almacenSesiones_ = std::make_unique<agente::AlmacenSesiones>();
    std::vector<agente::SesionChat> guardadas;
    try {
        guardadas = almacenSesiones_->cargarTodas();
    } catch (...) {
        // Sesiones ilegibles: se arranca con una nueva sin perder la TUI.
    }
    std::lock_guard<std::mutex> lock(chatMutex_);
    sesiones_.clear();
    for (auto& datos : guardadas) {
        auto s = std::make_shared<SesionTui>();
        s->datos = std::move(datos);
        s->cargarDesdeDatos();
        sesiones_.push_back(std::move(s));
    }
    sesionActiva_ = 0;
    if (sesiones_.empty()) {
        auto s = std::make_shared<SesionTui>();
        s->datos = agente::SesionChat::nueva("Sesión 1");
        sesiones_.push_back(std::move(s));
    }
}

std::shared_ptr<AplicacionTui::SesionTui> AplicacionTui::crearSesion(const std::string& titulo) {
    auto s = std::make_shared<SesionTui>();
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        s->datos = agente::SesionChat::nueva(titulo.empty() ? "Sesión " + std::to_string(sesiones_.size() + 1)
                                                            : titulo);
        sesiones_.push_back(s);
        sesionActiva_ = sesiones_.size() - 1;
    }
    guardarSesion(s);
    return s;
}

void AplicacionTui::irASesion(std::size_t indice) {
    std::string titulo;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        if (indice >= sesiones_.size()) return;
        sesionActiva_ = indice;
        titulo = sesiones_[indice]->datos.titulo;
    }
    mensaje_ = "Sesión " + std::to_string(indice + 1) + ": " + titulo;
    mensajeEsError_ = false;
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
}

void AplicacionTui::cambiarSesion(int delta) {
    std::size_t destino = 0;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        if (sesiones_.size() < 2) return;
        const int n = static_cast<int>(sesiones_.size());
        destino = static_cast<std::size_t>((static_cast<int>(sesionActiva_) + delta + n) % n);
    }
    irASesion(destino);
}

void AplicacionTui::guardarSesion(const std::shared_ptr<SesionTui>& sesion) {
    if (!almacenSesiones_ || !sesion) return;
    agente::SesionChat copia;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        // Mientras procesa, el hilo de la orden usa el historial: guarda él al terminar.
        if (sesion->ocupado) return;
        sesion->volcarEnDatos();
        sesion->datos.actualizada =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        copia = sesion->datos;
    }
    almacenSesiones_->guardar(copia);
}

bool AplicacionTui::ordenDeSesion(const std::string& linea) {
    if (linea.size() < 2 || linea[0] != '/') return false;
    std::istringstream in(linea.substr(1));
    std::string orden;
    in >> orden;
    std::string resto;
    std::getline(in, resto);
    resto.erase(0, resto.find_first_not_of(' '));
    static const std::vector<std::string> ordenes = {"nueva", "sesiones", "sesion", "renombrar",
                                                     "limpiar", "borrar", "cancelar", "ayuda"};
    if (std::find(ordenes.begin(), ordenes.end(), orden) == ordenes.end()) {
        return ordenDeSubagentes(orden, resto);
    }

    auto sesion = sesionActual();
    if (!sesion) return true;
    const auto informar = [&](const std::string& texto, bool error = false) {
        agregarMensajeChat(*sesionActual(), texto, error ? MensajeChat::Rol::Error : MensajeChat::Rol::Estado);
        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    };

    if (orden == "nueva") {
        crearSesion(resto);
        informar("Sesión nueva. Las demás siguen disponibles (/sesiones).");
    } else if (orden == "ayuda") {
        informar(kAyudaSesiones);
    } else if (orden == "sesiones") {
        std::string lista = "Sesiones:";
        {
            std::lock_guard<std::mutex> lock(chatMutex_);
            for (std::size_t i = 0; i < sesiones_.size(); ++i) {
                const auto& s = sesiones_[i];
                lista += "\n  " + std::to_string(i + 1) + (i == sesionActiva_ ? " ● " : "   ") + s->datos.titulo +
                         (s->ocupado ? "  (procesando)" : "") +
                         (s->datos.agente.proyectoActivo.empty() ? "" : "  · " + s->datos.agente.proyectoActivo);
            }
        }
        informar(lista + "\n/sesion N para cambiar.");
    } else if (orden == "sesion") {
        const int n = std::atoi(resto.c_str());
        std::size_t total = 0;
        {
            std::lock_guard<std::mutex> lock(chatMutex_);
            total = sesiones_.size();
        }
        if (n < 1 || static_cast<std::size_t>(n) > total) {
            informar("Uso: /sesion N, con N entre 1 y " + std::to_string(total), true);
        } else {
            irASesion(static_cast<std::size_t>(n - 1));
        }
    } else if (orden == "renombrar") {
        if (resto.empty()) {
            informar("Uso: /renombrar nuevo título", true);
        } else {
            {
                std::lock_guard<std::mutex> lock(chatMutex_);
                sesion->datos.titulo = resto;
            }
            guardarSesion(sesion);
            informar("Sesión renombrada: " + resto);
        }
    } else if (orden == "cancelar") {
        if (sesion->ocupado) {
            sesion->datos.agente.cancelar->store(true);
            informar("Cancelando la respuesta en curso...");
        } else {
            informar("No hay nada que cancelar en esta sesión.");
        }
    } else if (sesion->ocupado) {
        informar("Esta sesión está procesando: espera o cancela con Ctrl+X.", true);
    } else if (orden == "limpiar") {
        {
            std::lock_guard<std::mutex> lock(chatMutex_);
            sesion->mensajes.clear();
            sesion->datos.agente.historial.clear();
        }
        guardarSesion(sesion);
        informar("Conversación vaciada.");
    } else if (orden == "borrar") {
        if (resto != "confirmar") {
            informar("Esto borra «" + sesion->datos.titulo + "» y su conversación. Escribe /borrar confirmar.");
            return true;
        }
        if (almacenSesiones_) almacenSesiones_->borrar(sesion->datos.id);
        bool vacia = false;
        {
            std::lock_guard<std::mutex> lock(chatMutex_);
            sesiones_.erase(std::remove(sesiones_.begin(), sesiones_.end(), sesion), sesiones_.end());
            vacia = sesiones_.empty();
            sesionActiva_ = std::min(sesionActiva_, sesiones_.empty() ? 0 : sesiones_.size() - 1);
        }
        if (vacia) crearSesion("");
        informar("Sesión borrada.");
    }
    return true;
}

ftxui::Element AplicacionTui::barraSesiones(int ancho) {
    std::vector<std::pair<std::string, bool>> pestanas;
    std::size_t activa = 0;
    std::vector<bool> ocupadas;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        activa = sesionActiva_;
        for (std::size_t i = 0; i < sesiones_.size(); ++i) {
            std::string t = sesiones_[i]->datos.titulo;
            if (ftxui::string_width(t) > 24) t = recortar(t, 24);
            pestanas.push_back({" " + std::to_string(i + 1) + " " + t + (sesiones_[i]->ocupado ? " ⟳" : "") + " ",
                                i == activa});
        }
    }
    const std::string ayuda = " Ctrl+N nueva · Ctrl+←/→ ";
    const int disponible = std::max(10, ancho - ftxui::string_width(ayuda));
    // Ventana de pestañas que contiene a la activa.
    std::size_t desde = activa, hasta = activa + 1;
    int usado = pestanas.empty() ? 0 : ftxui::string_width(pestanas[activa].first);
    while (true) {
        bool crecio = false;
        if (hasta < pestanas.size() && usado + ftxui::string_width(pestanas[hasta].first) <= disponible) {
            usado += ftxui::string_width(pestanas[hasta++].first);
            crecio = true;
        }
        if (desde > 0 && usado + ftxui::string_width(pestanas[desde - 1].first) <= disponible) {
            usado += ftxui::string_width(pestanas[--desde].first);
            crecio = true;
        }
        if (!crecio) break;
    }
    ftxui::Elements elementos;
    if (desde > 0) elementos.push_back(ftxui::text("‹") | ftxui::color(kTextoTenue));
    for (std::size_t i = desde; i < hasta && i < pestanas.size(); ++i) {
        auto e = ftxui::text(pestanas[i].first);
        elementos.push_back(pestanas[i].second ? (e | ftxui::bold | ftxui::bgcolor(kFondoSeleccion) | ftxui::color(kExito))
                                               : (e | ftxui::color(kTextoTenue)));
    }
    if (hasta < pestanas.size()) elementos.push_back(ftxui::text("›") | ftxui::color(kTextoTenue));
    elementos.push_back(ftxui::filler());
    elementos.push_back(ftxui::text(ayuda) | ftxui::color(kTextoTenue));
    return ftxui::hbox(std::move(elementos));
}

}  // namespace funshi
