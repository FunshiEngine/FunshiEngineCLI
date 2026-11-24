/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — selector de color abierto desde la sección Configuración.
// Mientras está abierto recibe todos los eventos: el ratón mueve el punto o
// el tono (presionar y arrastrar), el color se aplica en vivo a toda la TUI,
// Enter lo guarda y Esc devuelve el original.
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/PreferenciasTui.h"

namespace funshi {

namespace {

void aplicarEnVivo(const tui::SelectorColor& s) {
    std::string error;
    tui::fijarColor(s.clave, tui::aHex(s.rgb()), error);
}

}  // namespace

void AplicacionTui::abrirSelectorColor(const std::string& clave) {
    const tui::ColorTema* c = tui::colorTema(clave);
    if (!c) return;
    selectorColor_.abrir(c->clave, c->nombre, c->actual);
    mensaje_ = "Color: " + std::string(c->nombre) + " · arrastra con el ratón · Enter aplica · Esc cancela";
    mensajeEsError_ = false;
}

ftxui::Element AplicacionTui::capaSelectorColor() {
    return selectorColor_.abierto ? selectorColor_.dibujar() : ftxui::emptyElement();
}

bool AplicacionTui::eventoSelectorColor(ftxui::Event evento) {
    auto& s = selectorColor_;
    using Zona = tui::SelectorColor::Zona;
    if (evento.is_mouse()) {
        const ftxui::Mouse& raton = evento.mouse();
        if (raton.button == ftxui::Mouse::Left && raton.motion == ftxui::Mouse::Pressed) {
            s.arrastrando = s.zonaEn(raton.x, raton.y);
        } else if (raton.motion == ftxui::Mouse::Released) {
            if (s.arrastrando != Zona::Ninguna) s.fijarDesde(s.arrastrando, raton.x, raton.y);
            s.arrastrando = Zona::Ninguna;
        } else if (raton.button == ftxui::Mouse::WheelUp || raton.button == ftxui::Mouse::WheelDown) {
            s.girarTono(raton.button == ftxui::Mouse::WheelUp ? 5.0 : -5.0);
            aplicarEnVivo(s);
            return true;
        }
        if (s.arrastrando != Zona::Ninguna) {
            s.fijarDesde(s.arrastrando, raton.x, raton.y);
            aplicarEnVivo(s);
        }
        return true;  // el ratón no llega a lo que está debajo
    }

    bool cambio = true;
    if (evento == ftxui::Event::ArrowLeft) s.mover(-1, 0);
    else if (evento == ftxui::Event::ArrowRight) s.mover(1, 0);
    else if (evento == ftxui::Event::ArrowUp) s.mover(0, -1);
    else if (evento == ftxui::Event::ArrowDown) s.mover(0, 1);
    else if (evento == ftxui::Event::Character('[') || evento == ftxui::Event::PageDown) s.girarTono(-5.0);
    else if (evento == ftxui::Event::Character(']') || evento == ftxui::Event::PageUp) s.girarTono(5.0);
    else cambio = false;
    if (cambio) {
        aplicarEnVivo(s);
        return true;
    }

    if (evento == ftxui::Event::Return) {
        aplicarEnVivo(s);
        tui::guardarPreferencias();
        mensaje_ = s.nombre + ": " + tui::aHex(s.rgb()) + " guardado";
        mensajeEsError_ = false;
        s.abierto = false;
        recargarSeccionActual(false);
        return true;
    }
    if (evento == ftxui::Event::Escape) {
        std::string error;
        tui::fijarColor(s.clave, tui::aHex(s.original), error);
        mensaje_ = s.nombre + ": sin cambios";
        mensajeEsError_ = false;
        s.abierto = false;
        return true;
    }
    if (evento == ftxui::Event::Character('x')) {
        // Escribir el valor a mano en la caja de comandos.
        std::string error;
        tui::fijarColor(s.clave, tui::aHex(s.original), error);
        s.abierto = false;
        textoComando_ = "tui.color " + s.clave + "=" + tui::aHex(s.original);
        cursorComando_ = static_cast<int>(textoComando_.size());
        focoEnEntrada_ = true;
        mensaje_ = "Escribe #RRGGBB o r,g,b y Enter · vacío = por defecto · Esc cancela";
        mensajeEsError_ = false;
        return true;
    }
    if (evento == ftxui::Event::CtrlC || evento == ftxui::Event::Character('\x03')) {
        s.abierto = false;
        pantalla_->Exit();
        return true;
    }
    return true;  // modal: el resto de las teclas no hace nada
}

}  // namespace funshi
