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
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

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

namespace {

std::string vistaValor(const nlohmann::json& valor) {
    if (valor.is_string()) return "texto \"" + valor.get<std::string>() + "\"";
    return nombreTipo(valor.type());
}

bool enteroDesdeTexto(const std::string& texto, long long& salida) {
    if (texto.empty()) return false;
    std::size_t usado = 0;
    errno = 0;
    const long long v = std::strtoll(texto.c_str(), nullptr, 10);
    std::size_t i = 0;
    if (texto[i] == '+' || texto[i] == '-') ++i;
    if (i == texto.size()) return false;
    for (; i < texto.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(texto[i]))) return false;
    }
    if (errno == ERANGE) return false;
    salida = v;
    return true;
}

bool unsignedDesdeTexto(const std::string& texto, unsigned long long& salida) {
    if (texto.empty() || texto[0] == '-') return false;
    std::size_t i = texto[0] == '+' ? 1 : 0;
    if (i == texto.size()) return false;
    for (; i < texto.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(texto[i]))) return false;
    }
    errno = 0;
    const unsigned long long v = std::strtoull(texto.c_str(), nullptr, 10);
    if (errno == ERANGE) return false;
    salida = v;
    return true;
}

bool numeroDesdeTexto(const std::string& texto, double& salida) {
    if (texto.empty()) return false;
    char* fin = nullptr;
    errno = 0;
    const double v = std::strtod(texto.c_str(), &fin);
    if (errno == ERANGE || fin == texto.c_str() || *fin != '\0') return false;
    if (!std::isfinite(v)) return false;
    salida = v;
    return true;
}

bool coaccionarTipo(nlohmann::json& valor, nlohmann::json::value_t tipo) {
    if (valor.type() == tipo) return true;
    // nlohmann usa number_integer y number_unsigned como tipos distintos.
    if (tipo == nlohmann::json::value_t::number_integer) {
        if (valor.is_number_unsigned()) {
            const auto v = valor.get<unsigned long long>();
            if (v > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) return false;
            valor = static_cast<long long>(v);
            return true;
        }
        if (valor.is_string()) {
            long long v = 0;
            if (!enteroDesdeTexto(valor.get<std::string>(), v)) return false;
            valor = v;
            return true;
        }
    }
    if (tipo == nlohmann::json::value_t::number_unsigned) {
        if (valor.is_number_integer()) {
            const auto v = valor.get<long long>();
            if (v < 0) return false;
            valor = static_cast<unsigned long long>(v);
            return true;
        }
        if (valor.is_string()) {
            unsigned long long v = 0;
            if (!unsignedDesdeTexto(valor.get<std::string>(), v)) return false;
            valor = v;
            return true;
        }
    }
    if (tipo == nlohmann::json::value_t::number_float) {
        if (valor.is_number()) {
            valor = valor.get<double>();
            return true;
        }
        if (valor.is_string()) {
            double v = 0.0;
            if (!numeroDesdeTexto(valor.get<std::string>(), v)) return false;
            valor = v;
            return true;
        }
    }
    if (tipo == nlohmann::json::value_t::boolean) {
        if (valor.is_string()) {
            const std::string t = valor.get<std::string>();
            if (t == "true") { valor = true; return true; }
            if (t == "false") { valor = false; return true; }
        }
    }
    return false;
}

}  // namespace

static bool validarArgumentosImpl(nlohmann::json& argumentos,
                                   const std::vector<RequisitoArg>& requisitos,
                                   std::string& error) {
    for (const RequisitoArg& req : requisitos) {
        if (!argumentos.contains(req.nombre)) {
            if (req.requerido) {
                error = "args." + req.nombre + " (" + nombreTipo(req.tipo) + ") es requerido";
                return false;
            }
            continue;
        }
        nlohmann::json& valor = argumentos[req.nombre];
        if (valor.type() == req.tipo) continue;
        if (coaccionarTipo(valor, req.tipo)) continue;
        error = "args." + req.nombre + " debe ser un " + nombreTipo(req.tipo) +
                " (recibido " + vistaValor(valor) + ")";
        if ((req.tipo == nlohmann::json::value_t::number_unsigned ||
             req.tipo == nlohmann::json::value_t::number_integer ||
             req.tipo == nlohmann::json::value_t::number_float) &&
            valor.is_string()) {
            error += "; usa un número JSON sin comillas";
        } else if (req.tipo == nlohmann::json::value_t::boolean && valor.is_string()) {
            error += "; usa true o false sin comillas";
        }
        return false;
    }
    return true;
}

bool validarArgumentos(nlohmann::json& argumentos,
                       const std::vector<RequisitoArg>& requisitos,
                       std::string& error) {
    return validarArgumentosImpl(argumentos, requisitos, error);
}

bool validarArgumentos(const nlohmann::json& argumentos,
                       const std::vector<RequisitoArg>& requisitos,
                       std::string& error) {
    nlohmann::json copia = argumentos;
    return validarArgumentosImpl(copia, requisitos, error);
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
