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
#include "Comandos/ComandosDiagnosticos.h"

#include <Diagnosticos/RegistroDiagnosticos.h>

#include "Nucleo/Registro.h"

#include <algorithm>
#include <filesystem>

namespace funshi {

namespace {

// Serializa un evento de diagnóstico a JSON.
nlohmann::json eventoJson(const EventoDiagnostico& evento) {
    return {{"timestamp", evento.fechaUtc},
            {"level", RegistroDiagnosticos::nombreNivel(evento.nivel)},
            {"module", evento.modulo},
            {"message", evento.mensaje},
            {"project", evento.proyecto},
            {"exception", evento.tipoExcepcion},
            {"stack", evento.traza}};
}

}  // namespace

ResultadoComando diagnosticoListar(const nlohmann::json& argumentos) {
    std::size_t limite = 100;
    if (argumentos.contains("limit")) {
        const nlohmann::json& valorLimite = argumentos["limit"];
        if (!valorLimite.is_number_integer()) {
            const std::string mensaje = "args.limit debe ser un entero no negativo";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje,
                                           codigosSalida::kErrorUso);
        }
        if (valorLimite.is_number_unsigned()) {
            limite = static_cast<std::size_t>(
                std::min<unsigned long long>(valorLimite.get<unsigned long long>(), 1000));
        } else {
            const long long valorEntero = valorLimite.get<long long>();
            if (valorEntero < 0) {
                const std::string mensaje = "args.limit debe ser un entero no negativo";
                registrarErrorUso("invalid_request", mensaje);
                return ResultadoComando::fallo("invalid_request", mensaje,
                                               codigosSalida::kErrorUso);
            }
            limite = static_cast<std::size_t>(std::min<long long>(valorEntero, 1000));
        }
    }
    const auto eventos = RegistroDiagnosticos::leerArchivo(
        RegistroDiagnosticos::archivoPorDefecto(), limite);
    nlohmann::json resultado = nlohmann::json::array();
    for (const auto& evento : eventos) resultado.push_back(eventoJson(evento));
    return ResultadoComando::exito(
        {{"items", std::move(resultado)}, {"count", eventos.size()}});
}

ResultadoComando diagnosticoLimpiar(const nlohmann::json&) {
    std::error_code ec;
    std::filesystem::remove(RegistroDiagnosticos::archivoPorDefecto(), ec);
    if (ec) {
        const std::string mensaje =
            "No se pudo borrar el archivo de diagnosticos: " + ec.message();
        registrarErrorOperacional("clear_failed", mensaje);
        return ResultadoComando::fallo("clear_failed", mensaje,
                                       codigosSalida::kFalloEscritura);
    }
    registrarEvento("Archivo de diagnosticos limpiado");
    return ResultadoComando::exito(
        {{"message", "Archivo de diagnosticos limpiado"}});
}

}  // namespace funshi
