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
#include "Nucleo/Respuesta.h"

namespace funshi {

Respuesta Respuesta::ok(nlohmann::json datos) {
    Respuesta respuesta;
    respuesta.correcta = true;
    respuesta.resultado = std::move(datos);
    return respuesta;
}

Respuesta Respuesta::fallo(std::string codigo, std::string mensaje) {
    Respuesta respuesta;
    respuesta.correcta = false;
    respuesta.codigoError = std::move(codigo);
    respuesta.mensajeError = std::move(mensaje);
    return respuesta;
}

Respuesta& Respuesta::conId(std::optional<nlohmann::json> identificador) {
    id = std::move(identificador);
    return *this;
}

Respuesta& Respuesta::conId(const nlohmann::json& identificador) {
    id = identificador;
    return *this;
}

nlohmann::json Respuesta::aJson() const {
    nlohmann::json cuerpo = {
        {"ok", correcta},
        {"result", correcta ? resultado : nlohmann::json(nullptr)},
        {"error", correcta
                      ? nlohmann::json(nullptr)
                      : nlohmann::json({{"code", codigoError},
                                        {"message", mensajeError}})},
        {"apiVersion", kVersionApiCli},
    };
    if (id.has_value()) cuerpo["id"] = *id;
    return cuerpo;
}

std::string Respuesta::aTexto() const {
    return aJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

ResultadoComando ResultadoComando::exito(nlohmann::json datos) {
    ResultadoComando resultado;
    resultado.respuesta = Respuesta::ok(std::move(datos));
    resultado.codigoSalida = codigosSalida::kExito;
    return resultado;
}

ResultadoComando ResultadoComando::fallo(std::string codigo, std::string mensaje,
                                         int codigoSalida) {
    ResultadoComando resultado;
    resultado.respuesta = Respuesta::fallo(std::move(codigo), std::move(mensaje));
    resultado.codigoSalida = codigoSalida;
    return resultado;
}

}  // namespace funshi
