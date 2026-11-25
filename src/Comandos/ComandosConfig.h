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
#ifndef FUNSHI_COMANDOS_CONFIG_H
#define FUNSHI_COMANDOS_CONFIG_H

#include <nlohmann/json.hpp>

#include "Nucleo/Respuesta.h"

namespace funshi {

// cli.config.get: devuelve la configuración persistida de rutas/directorios.
ResultadoComando cliConfigLeer(const nlohmann::json& argumentos);

// cli.config.set: actualiza y persiste la configuración de rutas/directorios.
ResultadoComando cliConfigEscribir(const nlohmann::json& argumentos);

// Lista modelos disponibles usando la configuración LLM efectiva o los
// argumentos provider/baseUrl/apiKey indicados.
ResultadoComando cliModelosListar(const nlohmann::json& argumentos);

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_CONFIG_H
