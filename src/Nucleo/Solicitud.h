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
#ifndef FUNSHI_NUCLEO_SOLICITUD_H
#define FUNSHI_NUCLEO_SOLICITUD_H

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace funshi {

// Petición de operación, agnóstica del transporte que la trajo. El mismo
// struct sirve para stdin, un archivo o una llamada directa desde una UI.
struct Solicitud {
    // Texto del comando, p. ej. "project.create".
    std::string comando;
    // Argumentos del comando; vacío si la solicitud no trajo "args".
    nlohmann::json argumentos = nlohmann::json::object();
    // Identificador de correlación que el cliente pidió eco.
    std::optional<nlohmann::json> id;
    // Proyecto elegido por --project (o por el selector equivalente de una
    // UI). Vacío si el cliente no lo fijó.
    std::string rutaProyecto;
};

}  // namespace funshi

#endif  // FUNSHI_NUCLEO_SOLICITUD_H
