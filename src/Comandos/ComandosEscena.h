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
#ifndef FUNSHI_COMANDOS_ESCENA_H
#define FUNSHI_COMANDOS_ESCENA_H

#include <nlohmann/json.hpp>

#include "Nucleo/Respuesta.h"

namespace funshi {

// scene.list: lista las escenas (archivos .db/.txt) del proyecto.
ResultadoComando escenaListar(const nlohmann::json& argumentos);

// scene.objects: carga la escena y devuelve la jerarquía completa.
ResultadoComando escenaObjetos(const nlohmann::json& argumentos);

// scene.object.get: carga la escena y busca un objeto por id o tag.
ResultadoComando escenaObjetoObtener(const nlohmann::json& argumentos);

// scene.hierarchy: alias de scene.objects; carga y devuelve la jerarquía.
ResultadoComando escenaJerarquia(const nlohmann::json& argumentos);

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_ESCENA_H
