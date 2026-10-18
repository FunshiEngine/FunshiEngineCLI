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
#ifndef FUNSHI_TUI_INTERFAZ_TUI_H
#define FUNSHI_TUI_INTERFAZ_TUI_H

namespace funshi {

// Lanza la TUI de la CLI a pantalla completa en la terminal actual. Toma el
// control de la terminal hasta que el usuario sale (q, Esc o Ctrl+C) y
// devuelve el código de salida del proceso. La interfaz construye sus
// Solicitudes y consume los ResultadoComando de ejecutarSolicitud()
// directamente, sin pasar por stdin ni stdout.
int ejecutarTui();

}  // namespace funshi

#endif  // FUNSHI_TUI_INTERFAZ_TUI_H
