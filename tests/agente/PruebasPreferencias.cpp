/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: preferencias visuales de la TUI (colores e idioma) y su
// persistencia en tui.json.
#include "PruebasAgente.h"

#include "Tui/Terminal/PreferenciasTui.h"
#include "Tui/Terminal/TemaTui.h"
#include "Tui/Configuracion/SelectorColor.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <filesystem>
#include <fstream>

namespace pruebas_agente {

void probarPreferencias() {
    namespace tui = funshi::tui;
    tui::Rgb c;
    CHECK(tui::leerColor("#61afef", c) && c == (tui::Rgb{97, 175, 239}), "preferencias: #RRGGBB");
    CHECK(tui::leerColor("10, 20,30", c) && c == (tui::Rgb{10, 20, 30}), "preferencias: r,g,b");
    CHECK(!tui::leerColor("#12345", c) && !tui::leerColor("300,0,0", c) && !tui::leerColor("rojo", c),
          "preferencias: rechaza colores inválidos");
    CHECK(tui::aHex({97, 175, 239}) == "#61AFEF", "preferencias: a hexadecimal");

    CHECK(tui::coloresTema().size() == 11 && tui::colorTema("fondoAviso"), "preferencias: toda la paleta es editable");
    std::string error;
    CHECK(tui::fijarColor("acento", "#FF0000", error) && funshi::kAcento == ftxui::Color::RGB(255, 0, 0),
          "preferencias: el color cambia en vivo");
    CHECK(!tui::fijarColor("acento", "verde", error) && !error.empty(), "preferencias: error si el color no vale");
    CHECK(!tui::fijarColor("inexistente", "#000000", error), "preferencias: error si la clave no existe");

    // Guardar y cargar: solo se guardan los cambiados.
    const auto archivo = std::filesystem::current_path() / "prefs" / "tui.json";
    CHECK(tui::fijarIdioma("en") && !tui::fijarIdioma("xx"), "preferencias: idiomas conocidos");
    CHECK(tui::guardarPreferencias(archivo), "preferencias: se guardan");
    {
        std::ifstream in(archivo);
        const auto j = nlohmann::json::parse(in, nullptr, false);
        CHECK(j.value("idioma", "") == "en" && j["colores"].size() == 1 &&
                  j["colores"].value("acento", "") == "#FF0000",
              "preferencias: tui.json con idioma y solo los colores cambiados");
    }
    tui::restablecerColores();
    tui::fijarIdioma("es");
    CHECK(funshi::kAcento == ftxui::Color::RGB(97, 175, 239), "preferencias: restablecer vuelve al defecto");
    tui::cargarPreferencias(archivo);
    CHECK(funshi::kAcento == ftxui::Color::RGB(255, 0, 0) && tui::idiomaActual() == "en",
          "preferencias: se cargan al iniciar");
    CHECK(tui::fijarColor("acento", "", error) && tui::colorTema("acento")->actual == tui::colorTema("acento")->porDefecto,
          "preferencias: vacío = por defecto");
    tui::fijarIdioma("de");
    CHECK(tui::siguienteIdioma() == "es", "preferencias: el idioma recorre la lista");
    tui::fijarIdioma("es");

    // Selector de color: HSV ida y vuelta, y el ratón sobre las cajas dibujadas.
    for (const tui::Rgb c0 : {tui::Rgb{97, 175, 239}, tui::Rgb{255, 0, 0}, tui::Rgb{0, 0, 0}, tui::Rgb{18, 200, 90}}) {
        CHECK(tui::aRgb(tui::aHsv(c0)) == c0, "selector: HSV ida y vuelta " + tui::aHex(c0));
    }
    tui::SelectorColor sel;
    sel.abrir("acento", "Acento", {97, 175, 239});
    auto pantalla = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(30));
    ftxui::Render(pantalla, sel.dibujar());
    const auto& cq = sel.cajaCuadro;
    const auto& ct = sel.cajaTono;
    CHECK(cq.x_max - cq.x_min + 1 == tui::SelectorColor::kAncho && cq.y_max - cq.y_min + 1 == tui::SelectorColor::kAlto,
          "selector: el cuadro ocupa 36x12 en pantalla (" + std::to_string(cq.x_max - cq.x_min + 1) + "x" +
              std::to_string(cq.y_max - cq.y_min + 1) + ")");
    CHECK(ct.y_min > cq.y_max && ct.x_max - ct.x_min + 1 == tui::SelectorColor::kAncho, "selector: slider de tono bajo el cuadro");
    using Zona = tui::SelectorColor::Zona;
    CHECK(sel.zonaEn(cq.x_min + 3, cq.y_min + 2) == Zona::Cuadro && sel.zonaEn(ct.x_min, ct.y_min) == Zona::Tono &&
              sel.zonaEn(0, 0) == Zona::Ninguna,
          "selector: zonas bajo el ratón");
    sel.fijarDesde(Zona::Cuadro, cq.x_max, cq.y_min);  // esquina superior derecha
    CHECK(sel.hsv.s == 1.0 && sel.hsv.v > 0.97, "selector: arriba a la derecha = saturado y brillante");
    sel.fijarDesde(Zona::Cuadro, cq.x_min - 10, cq.y_max + 10);  // arrastre fuera de la caja
    CHECK(sel.hsv.s == 0.0 && sel.hsv.v < 0.03, "selector: arrastrar fuera se queda en el borde");
    sel.fijarDesde(Zona::Tono, ct.x_max, ct.y_min);
    CHECK(sel.hsv.h == 360.0 || sel.hsv.h == 0.0 || sel.hsv.h > 359.0, "selector: tono al final del slider");
    sel.fijarDesde(Zona::Tono, (ct.x_min + ct.x_max) / 2, ct.y_min);
    CHECK(sel.hsv.h > 170 && sel.hsv.h < 190, "selector: tono a la mitad ≈ 180°");
    sel.hsv = {0, 1, 1};
    sel.mover(-100, 100);
    CHECK(sel.rgb() == (tui::Rgb{0, 0, 0}), "selector: teclado lleva el punto al negro");
    sel.girarTono(-5);
    CHECK(sel.hsv.h == 355.0, "selector: el tono da la vuelta");
    std::string dibujo = pantalla.ToString();
    CHECK(dibujo.find("Color: Acento") != std::string::npos && dibujo.find("#61AFEF") != std::string::npos,
          "selector: título y valor del color");
}

}  // namespace pruebas_agente
