/*
    FunshiEngineCLI - CLI de automación del motor FunshiEngineGL
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
#ifndef FUNSHI_NUCLEO_NUCLEO_CLI_H
#define FUNSHI_NUCLEO_NUCLEO_CLI_H

#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

namespace funshi {

// Núcleo de la CLI: recibe una Solicitud ya construida (por stdin, un archivo
// o una llamada directa de una futura UI) y devuelve el ResultadoComando con
// la respuesta y el código de salida. No toca stdout: esa decisión es del
// transporte. Tampoco lee la entrada: el parsing de argv y del JSON es
// responsabilidad del front-end.
ResultadoComando ejecutarSolicitud(const Solicitud& solicitud);

// Texto de ayuda en prosa (help | --help | -h). Lo consume el front-end de
// línea de comandos; una UI lo usaría para poblar su panel de ayuda.
std::string textoAyuda();

}  // namespace funshi

#endif  // FUNSHI_NUCLEO_NUCLEO_CLI_H
