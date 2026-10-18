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
#ifndef FUNSHI_NUCLEO_REGISTRO_H
#define FUNSHI_NUCLEO_REGISTRO_H

#include <string>

namespace funshi {

// Nombre del módulo con que la CLI firma sus eventos de diagnóstico.
extern const char* const kModuloCli;

// Error de uso: la solicitud llegó mal formada o le faltan argumentos. Solo
// se comunica al cliente; no ensucia el historial compartido del motor,
// porque un cliente que pide mal no es una falla del motor.
void registrarErrorUso(const std::string& codigo, const std::string& mensaje);

// Error operacional: la operación se pidió bien y falló al ejecutarse. Sí se
// registra en el historial compartido, junto a la ventana que ve la GUI.
void registrarErrorOperacional(const std::string& codigo,
                               const std::string& mensaje);

// Evento informativo del historial compartido.
void registrarEvento(const std::string& mensaje,
                     const std::string& proyecto = std::string());

}  // namespace funshi

#endif  // FUNSHI_NUCLEO_REGISTRO_H
