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
#include "Validacion/ValidacionArgs.h"

#include <algorithm>
#include <cctype>

namespace funshi {

namespace {

// Nombre legible de un tipo JSON, para mensajes de error de validación.
const char* nombreTipo(nlohmann::json::value_t tipo) {
    switch (tipo) {
        case nlohmann::json::value_t::string: return "string";
        case nlohmann::json::value_t::boolean: return "boolean";
        case nlohmann::json::value_t::number_integer: return "integer";
        case nlohmann::json::value_t::number_unsigned: return "unsigned";
        case nlohmann::json::value_t::number_float: return "float";
        case nlohmann::json::value_t::array: return "array";
        case nlohmann::json::value_t::object: return "object";
        default: return "null";
    }
}

}  // namespace

bool validarArgumentos(const nlohmann::json& argumentos,
                       const std::vector<RequisitoArg>& requisitos,
                       std::string& error) {
    for (const RequisitoArg& req : requisitos) {
        if (!argumentos.contains(req.nombre)) {
            if (req.requerido) {
                error = "args." + req.nombre + " (string) es requerido";
                return false;
            }
            continue;
        }
        const nlohmann::json& valor = argumentos[req.nombre];
        if (valor.type() != req.tipo) {
            error = "args." + req.nombre + " debe ser un " + nombreTipo(req.tipo);
            return false;
        }
    }
    return true;
}

bool argumentoProyecto(const nlohmann::json& argumentos,
                       std::string& nombreProyecto,
                       std::string& error) {
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
    };
    if (!validarArgumentos(argumentos, requisitos, error)) return false;
    nombreProyecto = argumentos["project"].get<std::string>();
    return true;
}

bool dimensionDesdeArgs(const nlohmann::json& argumentos,
                        DimensionProyecto& dimension) {
    if (!argumentos.contains("dimension")) {
        dimension = DimensionProyecto::TresD;
        return true;
    }
    const nlohmann::json& valor = argumentos["dimension"];
    if (valor.is_number_integer()) {
        dimension = dimensionProyectoDesdeEntero(valor.get<int>());
        return true;
    }
    if (valor.is_string()) {
        std::string texto = valor.get<std::string>();
        std::transform(texto.begin(), texto.end(), texto.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (texto == "2d" || texto == "dosd") {
            dimension = DimensionProyecto::DosD;
            return true;
        }
        if (texto == "3d" || texto == "tresd") {
            dimension = DimensionProyecto::TresD;
            return true;
        }
    }
    return false;
}

}  // namespace funshi
