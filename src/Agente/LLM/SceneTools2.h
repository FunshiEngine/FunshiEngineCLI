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

#pragma once

#include "EngineTools.h"

namespace funshi::agente::llm {

class SceneObjectSetTagTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.settag", "Establece el tag de un objeto",
            R"({"type":"object","properties":{"project":{"type":"string"},"scene":{"type":"string"},"object":{"type":"string"},"tag":{"type":"string"}},"required":["project","scene","object","tag"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.settag", args);
    }
};

class SceneObjectSetActiveTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.setactive", "Activa/desactiva un objeto",
            R"({"type":"object","properties":{"project":{"type":"string"},"scene":{"type":"string"},"object":{"type":"string"},"active":{"type":"boolean"}},"required":["project","scene","object","active"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.setactive", args);
    }
};

class SceneObjectSetFieldTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.setfield", "Establece un campo de un componente",
            R"({"type":"object","properties":{"project":{"type":"string"},"scene":{"type":"string"},"object":{"type":"string"},"component":{"type":"string"},"field":{"type":"string"},"value":{"type":"string"}},"required":["project","scene","object","component","field","value"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.setfield", args);
    }
};

class SceneAutoSaveTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.autosave", "Guarda automáticamente objetos dirty",
            R"({"type":"object","properties":{"project":{"type":"string"},"scene":{"type":"string"}},"required":["project","scene"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.autosave", args);
    }
};

} // namespace funshi::agente::llm