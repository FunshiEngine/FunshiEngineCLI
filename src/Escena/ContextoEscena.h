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
#ifndef FUNSHI_ESCENA_CONTEXTO_H
#define FUNSHI_ESCENA_CONTEXTO_H

#include <Assets/AssetManager.h>
#include <Assets/AssimpMeshLoader.h>
#include <Events/EventBus.h>
#include <Fisicas/PhysicsEngine.h>
#include <Scenes/EditorController.h>
#include <Scenes/SceneRegistry.h>
#include <Scenes/SceneSerializer.h>

#include <memory>
#include <string>

namespace funshi {

// Contexto de escena cargado desde disco, con el mismo armazón que usa el
// editor (registro, física, eventos, assets, controlador y serializador).
// Es RAII: al salir destruye todo en orden inverso al de construcción.
// Centraliza la carga para que todos los comandos de escena compartan una
// sola vía y no repitan el armazón a mano.
class ContextoEscena {
public:
    // Carga la escena del proyecto indicado. `error` recibe el motivo si la
    // carga no puede iniciarse.
    explicit ContextoEscena(const std::string& nombreProyecto);

    SceneRegistry& registro();
    EditorController& editor();

private:
    SceneRegistry registry_;
    PhysicsEngine physics_;
    EventBus events_;
    AssetManager assets_;
    EditorController editor_;
    SceneSerializer serializer_;
};

}  // namespace funshi

#endif  // FUNSHI_ESCENA_CONTEXTO_H
