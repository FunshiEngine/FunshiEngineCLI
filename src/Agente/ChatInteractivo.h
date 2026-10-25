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
#ifndef FUNSHI_AGENTE_CHAT_INTERACTIVO_H
#define FUNSHI_AGENTE_CHAT_INTERACTIVO_H

#include "Agente/Silvy.h"

#include <string>

namespace funshi {

// Chat interactivo de Silvy: bucle de lectura en stdin, interpreta órdenes
// con Silvy y muestra respuestas formateadas para un humano.
int ejecutarChatSilvy(Silvy& silvy);

}  // namespace funshi

#endif  // FUNSHI_AGENTE_CHAT_INTERACTIVO_H
