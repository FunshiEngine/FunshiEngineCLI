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

#include "Agente/LLM/Herramientas/EngineTools.h"

namespace funshi::agente::llm {

class SceneObjectSetTagTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.settag", "Establece el tag de un objeto",
            R"({"type":"object","properties":{"project":{"type":"string"},"id":{"type":"integer"},"tag":{"type":"string"}},"required":["project","id","tag"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.settag", args);
    }
};

class SceneObjectSetActiveTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.setactive", "Activa o desactiva un objeto",
            R"({"type":"object","properties":{"project":{"type":"string"},"id":{"type":"integer"},"active":{"type":"boolean"}},"required":["project","id","active"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.setactive", args);
    }
};

class SceneObjectSetFieldTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.setfield", "Establece un campo reflejado del componente Script de un objeto. value debe ser un objeto con la clave v (ej. {\"v\":123}, {\"v\":1.5}, {\"v\":\"texto\"}, {\"v\":true}, {\"v\":[x,y,z]}).",
            R"({"type":"object","properties":{"project":{"type":"string"},"id":{"type":"integer"},"field":{"type":"string"},"value":{"type":"object"}},"required":["project","id","field","value"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.setfield", args);
    }
};

class SceneAutoSaveTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.autosave", "Guarda automáticamente los objetos dirty de la escena del proyecto",
            R"({"type":"object","properties":{"project":{"type":"string"}},"required":["project"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.autosave", args);
    }
};

} // namespace funshi::agente::llm