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
#include "Escena/ContextoEscena.h"

#include <Configuracion/ProjectPaths.h>

namespace funshi {

ContextoEscena::ContextoEscena(const std::string& nombreProyecto)
    : assets_(std::make_unique<AssimpMeshLoader>()),
      editor_(&registry_, &physics_, &events_, &assets_),
      serializer_(&registry_, &editor_, &assets_) {
    editor_.setAssetManager(&assets_);
    const std::string pathTxt = ProjectPaths::rutaSceneBBDD(nombreProyecto);
    const std::string semiPath = ProjectPaths::directorioBinarios(nombreProyecto);
    serializer_.load(pathTxt, semiPath);
    editor_.registerSceneRigidBodies();
}

SceneRegistry& ContextoEscena::registro() {
    return registry_;
}

EditorController& ContextoEscena::editor() {
    return editor_;
}

}  // namespace funshi
