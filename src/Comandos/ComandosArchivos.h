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
#ifndef FUNSHI_COMANDOS_ARCHIVOS_H
#define FUNSHI_COMANDOS_ARCHIVOS_H

#include <nlohmann/json.hpp>

#include "Nucleo/Respuesta.h"

namespace funshi {

// project.src.create: crea un archivo o directorio dentro de src/<proyecto>.
ResultadoComando proyectoSrcCrear(const nlohmann::json& argumentos);

// project.dir.import: importa recursivamente una carpeta origen a src/<proyecto>.
ResultadoComando proyectoDirImportar(const nlohmann::json& argumentos);

// project.fs.create: crea un directorio dentro del vfs <proyecto>/fs.
ResultadoComando proyectoFsCrear(const nlohmann::json& argumentos);

// file.create: crea un archivo dentro del vfs <proyecto>/fs.
ResultadoComando archivoCrear(const nlohmann::json& argumentos);

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_ARCHIVOS_H
