/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Selector de color de la sección Configuración: un cuadro de saturación
// (x) por brillo (y) con un punto, y un slider de tono. Juntos cubren todo
// el espectro RGB. Este archivo tiene el modelo y el dibujo; la integración
// con eventos de la TUI está en AplicacionTui_SelectorColor.cpp.
//
// El cuadro usa media celda por muestra ('▀': frente = mitad de arriba,
// fondo = mitad de abajo), así tiene el doble de resolución vertical.

#include "Tui/Terminal/PreferenciasTui.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <string>

namespace funshi::tui {

struct Hsv {
    double h = 0.0;  // 0–360
    double s = 0.0;  // 0–1
    double v = 0.0;  // 0–1
};

Hsv aHsv(const Rgb& c);
Rgb aRgb(const Hsv& c);

struct SelectorColor {
    static constexpr int kAncho = 36;     // columnas del cuadro y del slider
    static constexpr int kAlto = 12;      // filas del cuadro (24 muestras)

    enum class Zona { Ninguna, Cuadro, Tono };

    bool abierto = false;
    std::string clave;    // color de la paleta que se edita
    std::string nombre;
    Hsv hsv;
    Rgb original;
    Zona arrastrando = Zona::Ninguna;
    // Cajas del último dibujo (reflect), para traducir el ratón.
    ftxui::Box cajaCuadro;
    ftxui::Box cajaTono;

    void abrir(const std::string& clave, const std::string& nombre, const Rgb& actual);
    Rgb rgb() const { return aRgb(hsv); }

    // Qué zona hay bajo (x, y) en pantalla.
    Zona zonaEn(int x, int y) const;
    // Fija saturación/brillo (Cuadro) o tono (Tono) desde una posición de
    // pantalla; fuera de la caja se usa el borde más cercano (para arrastrar).
    void fijarDesde(Zona zona, int x, int y);
    // Teclado: mueve el punto (en celdas del cuadro) y el tono (en grados).
    void mover(int dx, int dy);
    void girarTono(double grados);

    ftxui::Element dibujar();
};

}  // namespace funshi::tui
