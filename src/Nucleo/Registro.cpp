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
#include "Nucleo/Registro.h"

#include "Diagnosticos/RegistroDiagnosticos.h"

namespace funshi {

const char* const kModuloCli = "FunshiCLI";

void registrarErrorUso(const std::string& codigo, const std::string& mensaje) {
    // Política de registro: un error de uso solo se comunica al cliente a
    // través de la respuesta. No es una falla del motor, así que no entra en
    // el historial que consulta la ventana Estado.
    (void)codigo;
    (void)mensaje;
}

void registrarErrorOperacional(const std::string& codigo,
                               const std::string& mensaje) {
    EventoDiagnostico evento;
    evento.nivel = NivelDiagnostico::Error;
    evento.modulo = kModuloCli;
    evento.tipoExcepcion = codigo;
    evento.mensaje = mensaje;
    RegistroDiagnosticos::global().registrar(std::move(evento));
}

void registrarEvento(const std::string& mensaje,
                     const std::string& proyecto) {
    EventoDiagnostico evento;
    evento.nivel = NivelDiagnostico::Informacion;
    evento.modulo = kModuloCli;
    evento.proyecto = proyecto;
    evento.mensaje = mensaje;
    RegistroDiagnosticos::global().registrar(std::move(evento));
}

}  // namespace funshi
