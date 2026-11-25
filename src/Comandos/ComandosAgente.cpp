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
#include "Comandos/ComandosAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Nucleo/Registro.h"
#include "Validacion/ValidacionArgs.h"

#include <filesystem>

namespace funshi {

ResultadoComando agenteProcesar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    const std::vector<RequisitoArg> requisitos = {
        {"text", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    const std::string texto = argumentos["text"].get<std::string>();
    const std::filesystem::path rutaLog =
        std::filesystem::path(".agentes") / "silvy-auditoria.log";
    std::error_code ec;
    std::filesystem::create_directories(rutaLog.parent_path(), ec);

    Auditoria auditoria(rutaLog.string());
    Harness harness(auditoria);
    Silvy silvy(harness);
    const Silvy::RespuestaProcesada respuesta = silvy.procesar(texto);

    nlohmann::json rastro = {
        {"entradas", auditoria.entradas().size()},
        {"cambiosEnDisco", auditoria.cambiosEnDisco().size()},
        {"resumen", silvy.resumenQueHice(5)},
    };
    if (!auditoria.entradas().empty()) {
        const EntradaAuditoria& ultima = auditoria.entradas().back();
        rastro["ultimoComando"] = ultima.comando;
        rastro["ultimoOk"] = ultima.ok;
        rastro["ultimoCodigoSalida"] = ultima.codigoSalida;
        rastro["ultimoModificaDisco"] = ultima.modificaDisco;
    }

    nlohmann::json respuestaInterna = nullptr;
    bool comandoOk = false;
    if (respuesta.fueComandoCli) {
        respuestaInterna = respuesta.resultado.respuesta.aJson();
        comandoOk = respuesta.resultado.respuesta.correcta;
    }

    return ResultadoComando::exito({
        {"texto", respuesta.texto},
        {"fueComandoCli", respuesta.fueComandoCli},
        {"comandoOk", comandoOk},
        {"error", respuesta.error},
        {"respuesta", std::move(respuestaInterna)},
        {"auditoria", std::move(rastro)},
    });
}

}  // namespace funshi
