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
#include <memory>

namespace funshi::agente::llm {

class SceneListTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.list", "Lista las escenas de un proyecto",
            R"({"type":"object","properties":{"project":{"type":"string"}},"required":["project"]})"};
    }
    ToolCategory category() const override { return ToolCategory::NONE; }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.list", args);
    }
};

class SceneObjectCreateTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.create", "Crea un nuevo objeto en la escena",
            R"({"type":"object","properties":{"project":{"type":"string"},"name":{"type":"string"},"parentId":{"type":"integer"}},"required":["project","name"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.create", args);
    }
};

class SceneObjectGetTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.get", "Obtiene información de un objeto por id o tag",
            R"({"type":"object","properties":{"project":{"type":"string"},"id":{"type":"integer"},"tag":{"type":"string"}},"required":["project"]})"};
    }
    ToolCategory category() const override { return ToolCategory::NONE; }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.get", args);
    }
};

class SceneObjectSetParentTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.setparent", "Establece el padre de un objeto (parentId=0 para raíz)",
            R"({"type":"object","properties":{"project":{"type":"string"},"id":{"type":"integer"},"parentId":{"type":"integer"}},"required":["project","id","parentId"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.setparent", args);
    }
};

class SceneObjectCloneTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.clone", "Clona un objeto existente de la escena",
            R"({"type":"object","properties":{"project":{"type":"string"},"id":{"type":"integer"},"parentId":{"type":"integer"}},"required":["project","id"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.clone", args);
    }
};

class SceneObjectRemoveComponentTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.removecomponent", "Elimina un componente de un objeto",
            R"({"type":"object","properties":{"project":{"type":"string"},"id":{"type":"integer"},"componentType":{"type":"string"}},"required":["project","id","componentType"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.removecomponent", args);
    }
};

class SceneObjectUnparentTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"scene.object.unparent", "Desvincula un objeto de su padre y lo mueve a raíz",
            R"({"type":"object","properties":{"project":{"type":"string"},"id":{"type":"integer"}},"required":["project","id"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("scene.object.unparent", args);
    }
};

} // namespace funshi::agente::llm