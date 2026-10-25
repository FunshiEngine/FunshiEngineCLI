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

#include <string>
#include <optional>

namespace funshi::util {

// Abstracción cross-platform para portapapeles del sistema
// Linux: usa xclip (X11) o wl-copy (Wayland)
// Windows: usa Win32 API
class Clipboard {
public:
    Clipboard() = default;
    ~Clipboard() = default;

    // Copia texto al portapapeles del sistema
    // Retorna true si éxito, false si fallo
    static bool copy(const std::string& text);

    // Obtiene texto del portapapeles (opcional, para futuro uso)
    static std::optional<std::string> paste();

    // Verifica si el portapapeles está disponible
    static bool isAvailable();
};

}  // namespace funshi::util