/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Tui/Terminal/PreferenciasTui.h"

#include "Agente/LLM/Config/LlmConfig.h"
#include "Tui/Terminal/TemaTui.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

namespace funshi::tui {

namespace fs = std::filesystem;

namespace {

std::string& idiomaGuardado() {
    static std::string idioma = "es";
    return idioma;
}

void aplicar(ColorTema& c, const Rgb& valor) {
    c.actual = valor;
    *c.destino = ftxui::Color::RGB(valor.r, valor.g, valor.b);
}

}  // namespace

std::vector<ColorTema>& coloresTema() {
    static std::vector<ColorTema> v = {
        {"fondo", "Fondo", "fondo general y texto sobre la selección", &kFondo, {18, 18, 24}, {18, 18, 24}},
        {"fondoPanel", "Fondo de paneles", "panel central y chat", &kFondoPanel, {25, 26, 33}, {25, 26, 33}},
        {"fondoBarra", "Fondo de barras", "barras superior e inferior, cajas de texto", &kFondoBarra, {32, 34, 42}, {32, 34, 42}},
        {"fondoSeleccion", "Fondo de selección", "ítem seleccionado y pestaña activa", &kFondoSeleccion, {61, 65, 80}, {61, 65, 80}},
        {"fondoAviso", "Fondo de avisos", "aviso flotante (toast)", &kFondoAviso, {30, 30, 30}, {30, 30, 30}},
        {"texto", "Texto", "texto normal y respuestas de Silvy", &kTexto, {216, 219, 228}, {216, 219, 228}},
        {"textoTenue", "Texto tenue", "ayudas, notas y razonamiento", &kTextoTenue, {122, 126, 140}, {122, 126, 140}},
        {"acento", "Acento", "títulos, tus órdenes, resaltado del ratón", &kAcento, {97, 175, 239}, {97, 175, 239}},
        {"exito", "Éxito", "acciones que salieron bien", &kExito, {152, 195, 121}, {152, 195, 121}},
        {"advertencia", "Advertencia", "acciones en curso y avisos", &kAdvertencia, {229, 192, 123}, {229, 192, 123}},
        {"error", "Error", "errores y acciones fallidas", &kError, {224, 108, 117}, {224, 108, 117}},
    };
    return v;
}

ColorTema* colorTema(const std::string& clave) {
    for (auto& c : coloresTema()) {
        if (clave == c.clave) return &c;
    }
    return nullptr;
}

const std::vector<Idioma>& idiomas() {
    static const std::vector<Idioma> v = {
        {"es", "Español"}, {"en", "English"}, {"pt", "Português"}, {"fr", "Français"}, {"it", "Italiano"}, {"de", "Deutsch"},
    };
    return v;
}

bool leerColor(const std::string& texto, Rgb& salida) {
    std::string s;
    for (char c : texto) {
        if (!std::isspace(static_cast<unsigned char>(c))) s += c;
    }
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() == 6 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c); })) {
        const unsigned long v = std::stoul(s, nullptr, 16);
        salida = {static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v)};
        return true;
    }
    int r = -1, g = -1, b = -1;
    char resto = 0;
    if (std::sscanf(s.c_str(), "%d,%d,%d%c", &r, &g, &b, &resto) == 3 && r >= 0 && r <= 255 && g >= 0 &&
        g <= 255 && b >= 0 && b <= 255) {
        salida = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b)};
        return true;
    }
    return false;
}

std::string aHex(const Rgb& c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

bool fijarColor(const std::string& clave, const std::string& valor, std::string& error) {
    ColorTema* c = colorTema(clave);
    if (!c) {
        error = "No hay un color \"" + clave + "\"";
        return false;
    }
    if (valor.empty() || valor == "defecto" || valor == "\"\"") {
        aplicar(*c, c->porDefecto);
        return true;
    }
    Rgb rgb;
    if (!leerColor(valor, rgb)) {
        error = "Color inválido: usa #RRGGBB (por ejemplo #61AFEF) o r,g,b";
        return false;
    }
    aplicar(*c, rgb);
    return true;
}

void restablecerColores() {
    for (auto& c : coloresTema()) aplicar(c, c.porDefecto);
}

const std::string& idiomaActual() { return idiomaGuardado(); }

bool fijarIdioma(const std::string& codigo) {
    const auto& v = idiomas();
    if (std::none_of(v.begin(), v.end(), [&](const Idioma& i) { return codigo == i.codigo; })) return false;
    idiomaGuardado() = codigo;
    return true;
}

std::string siguienteIdioma() {
    const auto& v = idiomas();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (idiomaGuardado() == v[i].codigo) return v[(i + 1) % v.size()].codigo;
    }
    return v.front().codigo;
}

fs::path archivoPreferencias() { return agente::llm::LlmConfig::configPath().parent_path() / "tui.json"; }

void cargarPreferencias(const fs::path& archivo) {
    std::ifstream in(archivo);
    if (!in) return;
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) return;
    if (j.contains("idioma") && j["idioma"].is_string()) fijarIdioma(j["idioma"].get<std::string>());
    if (j.contains("colores") && j["colores"].is_object()) {
        for (const auto& [clave, valor] : j["colores"].items()) {
            std::string error;
            if (valor.is_string()) fijarColor(clave, valor.get<std::string>(), error);
        }
    }
}

bool guardarPreferencias(const fs::path& archivo) {
    nlohmann::json colores = nlohmann::json::object();
    for (const auto& c : coloresTema()) {
        if (c.actual != c.porDefecto) colores[c.clave] = aHex(c.actual);
    }
    const nlohmann::json j{{"idioma", idiomaGuardado()}, {"colores", colores}};
    std::error_code ec;
    fs::create_directories(archivo.parent_path(), ec);
    const fs::path tmp = archivo.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << j.dump(2);
        if (!out) return false;
    }
    fs::rename(tmp, archivo, ec);
    return !ec;
}

}  // namespace funshi::tui
