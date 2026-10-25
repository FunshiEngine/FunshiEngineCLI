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
#ifndef FUNSHI_AGENTE_HARNESS_H
#define FUNSHI_AGENTE_HARNESS_H

#include "Agente/Auditoria.h"
#include "Nucleo/NucleoCLI.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

#include <string>

namespace funshi {

// Harness del agente Silvy: ejecuta solicitudes contra el núcleo de la CLI y
// deja constancia de todo en la auditoría. Es la pata que conecta las órdenes
// de Silvy con ejecutarSolicitud().
class Harness {
public:
    explicit Harness(Auditoria& auditoria);

    // Ejecuta una solicitud, registra la entrada de auditoría y devuelve el
    // resultado. Si `modificaDisco` no se indica, se infiere del comando.
    ResultadoComando ejecutar(const Solicitud& solicitud,
                              bool modificaDisco = false);

    // Ejecuta directamente un comando por texto (atajo para pruebas/manual).
    ResultadoComando ejecutarTexto(const std::string& comandoTexto,
                                   const nlohmann::json& argumentos = nlohmann::json::object(),
                                   bool modificaDisco = false);

    const Auditoria& auditoria() const { return auditoria_; }

private:
    Auditoria& auditoria_;
};

}  // namespace funshi

#endif  // FUNSHI_AGENTE_HARNESS_H
