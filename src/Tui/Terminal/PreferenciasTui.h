/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Preferencias visuales de la TUI: idioma de la interfaz y cada color de la
// paleta (TemaTui.h). Se guardan en <carpeta de config>/tui.json:
//   {"idioma":"es","colores":{"acento":"#61AFEF", …}}
// Solo se guardan los colores cambiados; los demás quedan en su valor por
// defecto. El idioma se guarda y se muestra, pero los textos de la interfaz
// siguen en español.

#include <ftxui/screen/color.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace funshi::tui {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};

struct ColorTema {
    const char* clave;        // en tui.json ("acento")
    const char* nombre;       // en la interfaz ("Acento")
    const char* uso;          // dónde se ve
    ftxui::Color* destino;    // la variable de TemaTui.h
    Rgb porDefecto;
    Rgb actual;
};

struct Idioma {
    const char* codigo;  // "es"
    const char* nombre;  // "Español"
};

// Paleta editable, en el orden en que se muestra.
std::vector<ColorTema>& coloresTema();
ColorTema* colorTema(const std::string& clave);
const std::vector<Idioma>& idiomas();

// "#RRGGBB", "RRGGBB" o "r,g,b". Devuelve false si no es un color.
bool leerColor(const std::string& texto, Rgb& salida);
std::string aHex(const Rgb& c);

// Cambia un color en vivo (y lo deja para guardar). Texto vacío o "defecto"
// vuelve al valor por defecto. `error` explica por qué no se pudo.
bool fijarColor(const std::string& clave, const std::string& valor, std::string& error);
void restablecerColores();

const std::string& idiomaActual();
bool fijarIdioma(const std::string& codigo);
// Siguiente idioma de la lista (para recorrerlos con Enter).
std::string siguienteIdioma();

std::filesystem::path archivoPreferencias();
// Lee tui.json y aplica idioma y colores. Ignora valores inválidos.
void cargarPreferencias(const std::filesystem::path& archivo = archivoPreferencias());
bool guardarPreferencias(const std::filesystem::path& archivo = archivoPreferencias());

}  // namespace funshi::tui
