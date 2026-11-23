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
// Utilidades puras de la TUI: recorte de texto y atajos contextuales de la
// barra inferior. La sintaxis de línea de comandos está en Nucleo/LineaComando.
#ifndef FUNSHI_TUI_UTIL_TUI_H
#define FUNSHI_TUI_UTIL_TUI_H

#include "Nucleo/LineaComando.h"  // tokenizar / construirArgumentos (antes vivían aquí)

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace funshi {

// Índice del byte donde el texto alcanza el ancho indicado en columnas de
// pantalla, sin partir un carácter UTF-8.
std::size_t byteDeAncho(const std::string& texto, std::size_t anchoMaximo);

// Recorta un texto al ancho indicado en columnas de pantalla, sin cortar a
// mitad de un carácter UTF-8, y agrega puntos de continuación si se recortó.
std::string recortar(const std::string& texto, std::size_t anchoMaximo);

// Ancho útil de la terminal, con un mínimo para que la interfaz no se
// descomponga en ventanas muy angostas.
int anchoTerminal();

// Texto de atajos que cabe en la terminal actual.
std::string textoAtajos();


}  // namespace funshi

#endif  // FUNSHI_TUI_UTIL_TUI_H
