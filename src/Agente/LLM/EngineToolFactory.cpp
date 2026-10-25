/*
 * Copyright 2024 FunshiEngine
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "EngineTools.h"
#include "SceneTools.h"
#include "SceneTools2.h"
#include "OtherTools.h"
#include "FileTools.h"
#include "ToolRegistry.h"

namespace funshi::agente::llm {

std::shared_ptr<ToolRegistry> createEngineToolRegistry(std::shared_ptr<Harness> harness) {
    auto registry = std::make_shared<ToolRegistry>();
    registry->addAll({
        std::make_shared<ProjectCreateTool>(harness),
        std::make_shared<ProjectListTool>(harness),
        std::make_shared<ProjectConfigGetTool>(harness),
        std::make_shared<SceneListTool>(harness),
        std::make_shared<SceneObjectCreateTool>(harness),
        std::make_shared<SceneObjectGetTool>(harness),
        std::make_shared<SceneObjectSetParentTool>(harness),
        std::make_shared<SceneObjectCloneTool>(harness),
        std::make_shared<SceneObjectRemoveComponentTool>(harness),
        std::make_shared<SceneObjectUnparentTool>(harness),
        std::make_shared<SceneObjectSetTagTool>(harness),
        std::make_shared<SceneObjectSetActiveTool>(harness),
        std::make_shared<SceneObjectSetFieldTool>(harness),
        std::make_shared<SceneAutoSaveTool>(harness),
        std::make_shared<AssetImportTool>(harness),
        std::make_shared<ScriptListTool>(harness),
        std::make_shared<ScriptCompileTool>(harness),
        std::make_shared<DiagnosticsListTool>(harness),
        std::make_shared<DiagnosticsClearTool>(harness),
        std::make_shared<FileCreateTool>(harness),
        std::make_shared<DirImportTool>(harness),
        std::make_shared<FsCreateTool>(harness),
        std::make_shared<ProjectSrcCreateTool>(harness),
    });
    return registry;
}

} // namespace funshi::agente::llm