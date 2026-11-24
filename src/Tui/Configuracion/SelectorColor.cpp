/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Tui/Configuracion/SelectorColor.h"

#include "Tui/Terminal/TemaTui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace funshi::tui {

namespace {

constexpr int kMuestrasY = SelectorColor::kAlto * 2;

ftxui::Color aFtxui(const Rgb& c) { return ftxui::Color::RGB(c.r, c.g, c.b); }

double fraccion(int pos, int min, int max) {
    if (max <= min) return 0.0;
    return std::clamp(static_cast<double>(pos - min) / (max - min), 0.0, 1.0);
}

// Color que contrasta con `c` (para el punto y el marcador).
ftxui::Color contraste(const Rgb& c) {
    const double luz = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
    return luz > 140 ? ftxui::Color::RGB(0, 0, 0) : ftxui::Color::RGB(255, 255, 255);
}

}  // namespace

Hsv aHsv(const Rgb& c) {
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double max = std::max({r, g, b}), min = std::min({r, g, b}), d = max - min;
    Hsv out;
    out.v = max;
    out.s = max <= 0.0 ? 0.0 : d / max;
    if (d <= 0.0) {
        out.h = 0.0;
    } else if (max == r) {
        out.h = 60.0 * std::fmod((g - b) / d, 6.0);
    } else if (max == g) {
        out.h = 60.0 * ((b - r) / d + 2.0);
    } else {
        out.h = 60.0 * ((r - g) / d + 4.0);
    }
    if (out.h < 0.0) out.h += 360.0;
    return out;
}

Rgb aRgb(const Hsv& c) {
    const double h = std::fmod(std::fmod(c.h, 360.0) + 360.0, 360.0);
    const double s = std::clamp(c.s, 0.0, 1.0), v = std::clamp(c.v, 0.0, 1.0);
    const double cr = v * s;
    const double x = cr * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - cr;
    double r = 0, g = 0, b = 0;
    if (h < 60) { r = cr; g = x; }
    else if (h < 120) { r = x; g = cr; }
    else if (h < 180) { g = cr; b = x; }
    else if (h < 240) { g = x; b = cr; }
    else if (h < 300) { r = x; b = cr; }
    else { r = cr; b = x; }
    const auto canal = [&](double t) { return static_cast<std::uint8_t>(std::lround((t + m) * 255.0)); };
    return {canal(r), canal(g), canal(b)};
}

void SelectorColor::abrir(const std::string& claveColor, const std::string& nombreColor, const Rgb& actual) {
    abierto = true;
    clave = claveColor;
    nombre = nombreColor;
    original = actual;
    hsv = aHsv(actual);
    arrastrando = Zona::Ninguna;
}

SelectorColor::Zona SelectorColor::zonaEn(int x, int y) const {
    if (x >= cajaCuadro.x_min && x <= cajaCuadro.x_max && y >= cajaCuadro.y_min && y <= cajaCuadro.y_max) {
        return Zona::Cuadro;
    }
    // El slider admite el clic también en la fila del marcador, justo debajo.
    if (x >= cajaTono.x_min && x <= cajaTono.x_max && y >= cajaTono.y_min && y <= cajaTono.y_max + 1) {
        return Zona::Tono;
    }
    return Zona::Ninguna;
}

void SelectorColor::fijarDesde(Zona zona, int x, int y) {
    if (zona == Zona::Cuadro) {
        hsv.s = fraccion(x, cajaCuadro.x_min, cajaCuadro.x_max);
        // Cada fila son dos muestras; se toma el centro de la fila.
        const int fila = std::clamp(y - cajaCuadro.y_min, 0, kAlto - 1);
        hsv.v = 1.0 - std::clamp((fila * 2 + 0.5) / (kMuestrasY - 1), 0.0, 1.0);
    } else if (zona == Zona::Tono) {
        hsv.h = fraccion(x, cajaTono.x_min, cajaTono.x_max) * 360.0;
    }
}

void SelectorColor::mover(int dx, int dy) {
    hsv.s = std::clamp(hsv.s + dx / static_cast<double>(kAncho - 1), 0.0, 1.0);
    hsv.v = std::clamp(hsv.v - dy / static_cast<double>(kMuestrasY - 1), 0.0, 1.0);
}

void SelectorColor::girarTono(double grados) { hsv.h = std::fmod(hsv.h + grados + 360.0, 360.0); }

ftxui::Element SelectorColor::dibujar() {
    using namespace ftxui;
    const int colPunto = static_cast<int>(std::lround(hsv.s * (kAncho - 1)));
    const int muestraPunto = static_cast<int>(std::lround((1.0 - hsv.v) * (kMuestrasY - 1)));
    const auto muestra = [&](int col, int fila) {
        return aRgb({hsv.h, col / static_cast<double>(kAncho - 1), 1.0 - fila / static_cast<double>(kMuestrasY - 1)});
    };

    Elements filas;
    for (int fila = 0; fila < kAlto; ++fila) {
        Elements celdas;
        for (int col = 0; col < kAncho; ++col) {
            const Rgb arriba = muestra(col, fila * 2), abajo = muestra(col, fila * 2 + 1);
            if (col == colPunto && muestraPunto / 2 == fila) {
                const Rgb aqui = muestraPunto % 2 == 0 ? arriba : abajo;
                celdas.push_back(text("●") | color(contraste(aqui)) | bgcolor(aFtxui(aqui)));
            } else {
                celdas.push_back(text("▀") | color(aFtxui(arriba)) | bgcolor(aFtxui(abajo)));
            }
        }
        filas.push_back(hbox(std::move(celdas)));
    }
    Element cuadro = vbox(std::move(filas)) | reflect(cajaCuadro);

    // Slider de dos filas de alto: más fácil de acertar con el ratón.
    const int colTono = static_cast<int>(std::lround(hsv.h / 360.0 * (kAncho - 1)));
    const auto filaTono = [&]() {
        Elements tonos;
        for (int col = 0; col < kAncho; ++col) {
            tonos.push_back(text(" ") | bgcolor(aFtxui(aRgb({col * 360.0 / (kAncho - 1), 1.0, 1.0}))));
        }
        return hbox(std::move(tonos));
    };
    Element tono = vbox({filaTono(), filaTono()}) | reflect(cajaTono);
    const Element marcador = text(std::string(static_cast<std::size_t>(colTono), ' ') + "▲") | color(kTexto);

    const Rgb nuevo = rgb();
    char datos[96];
    std::snprintf(datos, sizeof(datos), " %s · %d,%d,%d · H %d° S %d%% V %d%% ", aHex(nuevo).c_str(), nuevo.r,
                  nuevo.g, nuevo.b, static_cast<int>(std::lround(hsv.h)), static_cast<int>(std::lround(hsv.s * 100)),
                  static_cast<int>(std::lround(hsv.v * 100)));
    const Element muestras = hbox({
        text(" nuevo ") | color(kTextoTenue), text("      ") | bgcolor(aFtxui(nuevo)),
        text("  original ") | color(kTextoTenue), text("      ") | bgcolor(aFtxui(original)),
    });

    // hbox + filler: el cuadro y el slider conservan su ancho (si el vbox los
    // estirara, la caja ya no coincidiría con las muestras y el ratón se correría).
    return window(text(" Color: " + nombre + " ") | bold | color(kAcento),
                  vbox({
                      hbox({cuadro, filler()}),
                      text(" saturación → · ↑ brillo") | color(kTextoTenue),
                      hbox({tono, filler()}),
                      marcador,
                      separatorLight() | color(kTextoTenue),
                      muestras,
                      text(datos) | color(kTexto),
                      text(" arrastra el punto o el tono · flechas · [ ] tono") | color(kTextoTenue),
                      text(" Enter aplica · Esc cancela · x escribir el valor") | color(kTextoTenue),
                  })) |
           bgcolor(kFondoPanel) | clear_under | center;
}

}  // namespace funshi::tui
