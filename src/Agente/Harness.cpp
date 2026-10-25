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
#include "Agente/Harness.h"

#include <nlohmann/json.hpp>

namespace funshi {

Harness::Harness(Auditoria& auditoria) : auditoria_(auditoria) {}

ResultadoComando Harness::ejecutar(const Solicitud& solicitud,
                                   bool modificaDisco) {
    if (!modificaDisco) {
        modificaDisco = auditoria_.esComandoDeEscritura(solicitud.comando);
    }

    ResultadoComando resultado = ejecutarSolicitud(solicitud);

    EntradaAuditoria entrada;
    entrada.marcaTiempo = auditoria_.ahoraIso8601();
    entrada.comando = solicitud.comando;
    entrada.argumentos = solicitud.argumentos;
    entrada.ok = resultado.respuesta.correcta;
    entrada.codigoError = resultado.respuesta.codigoError;
    entrada.mensajeError = resultado.respuesta.mensajeError;
    entrada.codigoSalida = resultado.codigoSalida;
    entrada.modificaDisco = modificaDisco;
    if (resultado.respuesta.id) {
        entrada.observaciones = "id=" + resultado.respuesta.id->dump();
    }

    auditoria_.registrar(entrada);
    return resultado;
}

ResultadoComando Harness::ejecutarTexto(const std::string& comandoTexto,
                                        const nlohmann::json& argumentos,
                                        bool modificaDisco) {
    Solicitud solicitud;
    solicitud.comando = comandoTexto;
    solicitud.argumentos = argumentos;
    return ejecutar(solicitud, modificaDisco);
}

}  // namespace funshi
