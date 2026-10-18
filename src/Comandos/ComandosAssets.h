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
#ifndef FUNSHI_COMANDOS_ASSETS_H
#define FUNSHI_COMANDOS_ASSETS_H

#include <nlohmann/json.hpp>

#include "Nucleo/Respuesta.h"

namespace funshi {

// asset.import: importa un archivo (modelo, textura o audio) al proyecto,
// clasificándolo por extensión en la subcarpeta correspondiente de src/.
ResultadoComando assetImportar(const nlohmann::json& argumentos);

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_ASSETS_H
