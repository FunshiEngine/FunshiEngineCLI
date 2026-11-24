/*
 * Copyright 2026 FunshiEngine
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <optional>
#include <string>

namespace funshi::util {

// Resultado de una copia: con qué se hizo o por qué falló.
struct ResultadoCopia {
    bool ok = false;
    std::string metodo;  // "wl-copy", "xclip", "xsel", "pbcopy", "tmux", "Win32", "OSC 52"
    std::string error;   // motivo legible si !ok
};

// Portapapeles del sistema, multiplataforma.
//
// Orden de métodos:
//   Windows: API Win32 (CF_UNICODETEXT, acentos correctos).
//   macOS:   pbcopy.
//   Linux:   wl-copy (Wayland) → xclip / xsel (X11) → tmux load-buffer -w (dentro de tmux).
// Sin ninguno (SSH, consola sin X) queda OSC 52: la secuencia de escape con la
// que la propia terminal copia. La CLI no puede escribirla por su cuenta sin
// pisar el dibujado de la TUI, así que la devuelve `secuenciaOsc52()` para que
// la escriba quien controla la salida.
class Clipboard {
public:
    // Copia con la herramienta del sistema. Puede tardar (lanza un proceso):
    // no llamar desde el hilo de dibujado.
    static ResultadoCopia copiar(const std::string& texto);

    // Hay alguna herramienta del sistema utilizable (detección cacheada).
    static bool hayHerramientaSistema();

    // Secuencia OSC 52 lista para escribir en la terminal, envuelta para tmux
    // o screen si hace falta. Vacía si el texto supera el límite habitual de
    // las terminales (~74 KB de texto).
    static std::string secuenciaOsc52(const std::string& texto);

    // Compatibilidad: copia y devuelve si funcionó.
    static bool copy(const std::string& text) { return copiar(text).ok; }

    // Obtiene texto del portapapeles (sin uso en la TUI por ahora).
    static std::optional<std::string> paste();

    // Equivale a hayHerramientaSistema().
    static bool isAvailable() { return hayHerramientaSistema(); }
};

}  // namespace funshi::util
