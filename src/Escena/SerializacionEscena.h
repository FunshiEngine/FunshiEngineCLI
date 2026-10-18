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
#ifndef FUNSHI_ESCENA_SERIALIZACION_H
#define FUNSHI_ESCENA_SERIALIZACION_H

#include <nlohmann/json.hpp>

#include <Objetos/Componentes/Component.h>
#include <Objetos/GameObject.h>
#include <Scenes/SceneRegistry.h>

#include <string>

namespace funshi {

// Serializa un componente a JSON: siempre expone el tipo y, si el componente
// es de un tipo conocido (Transform, Script, Model, Color), sus campos.
nlohmann::json componenteJson(Component* comp);

// Serializa un GameObject a JSON: id, nombre, tag, activo, transform local,
// componentes y hijos recursivos.
nlohmann::json gameObjectJson(GameObject* obj);

// Serializa la jerarquía completa de la escena (hijos del root) a un array.
nlohmann::json sceneHierarchyJson(SceneRegistry* registry);

// Búsqueda recursiva por id (0 = root) o por tag.
GameObject* findObjectById(SceneRegistry* registry, int id);
GameObject* findObjectByTag(SceneRegistry* registry, const std::string& tag);

}  // namespace funshi

#endif  // FUNSHI_ESCENA_SERIALIZACION_H
