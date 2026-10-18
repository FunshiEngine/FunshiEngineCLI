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
#ifndef FUNSHI_VALIDACION_ARGS_H
#define FUNSHI_VALIDACION_ARGS_H

#include <nlohmann/json.hpp>

#include <Configuracion/DimensionProyecto.h>
#include <Configuracion/ProjectManager.h>

#include <string>

namespace funshi {

// Argumento de solicitud con tipo y obligatoriedad. La tabla describe de
// forma declarativa qué espera cada comando, de modo que la validación sea
// una sola función y un comando nuevo solo agregue su fila.
struct RequisitoArg {
    std::string nombre;
    bool requerido;
    nlohmann::json::value_t tipo;
};

// Valida los argumentos de un comando contra su tabla de requisitos. Si algo
// falta o tiene tipo incorrecto, `error` recibe el mensaje y devuelve false.
bool validarArgumentos(const nlohmann::json& argumentos,
                       const std::vector<RequisitoArg>& requisitos,
                       std::string& error);

// Extrae args.project (string requerido por varios comandos de escena).
bool argumentoProyecto(const nlohmann::json& argumentos,
                       std::string& nombreProyecto,
                       std::string& error);

// Interpreta args.dimension: entero 2/3 o los textos "2d"/"3d"/"DosD"/"TresD"
// (sin distinguir mayusculas). Ausente -> 3D (retrocompatible con proyectos
// previos al campo). Devuelve false si el valor esta presente pero no se
// reconoce, para que el llamador responda invalid_request en lugar de asumir 3D.
bool dimensionDesdeArgs(const nlohmann::json& argumentos,
                        DimensionProyecto& dimension);

}  // namespace funshi

#endif  // FUNSHI_VALIDACION_ARGS_H
