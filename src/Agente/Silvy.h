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
#ifndef FUNSHI_AGENTE_SILVY_H
#define FUNSHI_AGENTE_SILVY_H

#include "Agente/Harness.h"
#include "Nucleo/Solicitud.h"

#include <string>
#include <vector>

namespace funshi {

// Orden procesada por Silvy: texto original, solicitud generada y si se logró
// interpretar.
struct OrdenProcesada {
    std::string textoOriginal;
    Solicitud solicitud;
    bool interpretada = false;
    std::string motivoRechazo;
};

// Silvy: agente que recibe órdenes (texto natural o JSON), las traduce a
// solicitudes de la CLI y las ejecuta a través del harness. Mantiene un
// historial de lo que hizo para poder responder "qué cambiaste".
class Silvy {
public:
    explicit Silvy(Harness& harness);

    // Interpreta una orden de texto. Si es JSON válido, lo usa directo.
    // Si no, aplica patrones simples (crear proyecto, listar, etc.).
    OrdenProcesada interpretar(const std::string& texto);

    // Interpreta y ejecuta. Devuelve el ResultadoComando.
    ResultadoComando ejecutarOrden(const std::string& texto);

    // Devuelve el resumen de auditoría para mostrar al usuario qué se hizo.
    std::string resumenQueHice(std::size_t n = 10) const;

    const Harness& harness() const { return harness_; }

private:
    Harness& harness_;
    std::vector<OrdenProcesada> historial_;

    bool esJsonValido(const std::string& texto) const;
    OrdenProcesada interpretarTextoPlano(const std::string& texto);
};

}  // namespace funshi

#endif  // FUNSHI_AGENTE_SILVY_H
