/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    SPDX-License-Identifier: Apache-2.0
*/
#include "Tui/Terminal/UtilTui.h"

#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace funshi {

std::size_t byteDeAncho(const std::string& texto, std::size_t anchoMaximo) {
    std::size_t ancho = 0;
    std::size_t i = 0;
    while (i < texto.size()) {
        // Longitud del carácter UTF-8 en bytes.
        const unsigned char byte = static_cast<unsigned char>(texto[i]);
        std::size_t largo = 1;
        if ((byte & 0xE0) == 0xC0) {
            largo = 2;
        } else if ((byte & 0xF0) == 0xE0) {
            largo = 3;
        } else if ((byte & 0xF8) == 0xF0) {
            largo = 4;
        }
        if (i + largo > texto.size()) break;
        const int anchoCaracter = ftxui::string_width(texto.substr(i, largo));
        if (ancho + static_cast<std::size_t>(anchoCaracter) > anchoMaximo) break;
        ancho += static_cast<std::size_t>(anchoCaracter);
        i += largo;
    }
    return i;
}

std::string recortar(const std::string& texto, std::size_t anchoMaximo) {
    if (ftxui::string_width(texto) <= static_cast<int>(anchoMaximo)) return texto;
    if (anchoMaximo <= 3) return texto.substr(0, byteDeAncho(texto, anchoMaximo));
    return texto.substr(0, byteDeAncho(texto, anchoMaximo - 3)) + "...";
}

int anchoTerminal() {
    return std::max(40, ftxui::Terminal::Size().dimx);
}

// Atajos de la barra inferior, de la versión más completa a la más corta: se
// usa la primera que cabe en el ancho de la terminal, para que en ventanas
// angostas la línea no quede cortada a media palabra.
const std::vector<std::string>& atajosBarraInferior() {
    static const std::vector<std::string> atajos = {
        " ↑↓ mover · ←→ sección · enter usar · r recargar · / comando · ? ayuda · esc/q salir ",
        " ↑↓ mover · ←→ sección · enter usar · / comando · ? ayuda · esc/q salir ",
        " ↑↓ mover · enter usar · / comando · ? ayuda · esc/q salir ",
    };
    return atajos;
}

std::string textoAtajos() {
    const std::size_t ancho = static_cast<std::size_t>(anchoTerminal());
    for (const std::string& atajos : atajosBarraInferior()) {
        if (ftxui::string_width(atajos) <= static_cast<int>(ancho)) return atajos;
    }
    return recortar(atajosBarraInferior().back(), ancho);
}


}  // namespace funshi
