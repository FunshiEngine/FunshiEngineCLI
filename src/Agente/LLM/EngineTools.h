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

#include "ToolRegistry.h"
#include "../Harness.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>

namespace funshi::agente::llm {

/**
 * Base para tools que ejecutan comandos del motor via Harness.
 */
class EngineTool : public Tool {
public:
    explicit EngineTool(std::shared_ptr<Harness> harness) : harness_(std::move(harness)) {}

    ToolCategory category() const override { return ToolCategory::FILES; }

protected:
    std::shared_ptr<Harness> harness_;

    ToolResult executeCommand(const std::string& command, const nlohmann::json& args) {
        if (!harness_) {
            return ToolResult::error("Harness no disponible");
        }
        funshi::Solicitud sol;
        sol.comando = command;
        sol.argumentos = args;
        sol.id = "llm-" + std::to_string(std::hash<std::string>{}(command + args.dump()));

        auto resultado = harness_->ejecutar(sol);
        if (resultado.respuesta.correcta) {
            return ToolResult::ok(resultado.respuesta.resultado.dump());
        } else {
            return ToolResult::error(resultado.respuesta.mensajeError);
        }
    }
};

class ProjectCreateTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{
            "project.create",
            "Crea un nuevo proyecto (2D o 3D)",
            R"({"type":"object","properties":{"name":{"type":"string"},"dimension":{"type":"integer","enum":[2,3]}},"required":["name","dimension"]})"
        };
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("project.create", args);
    }
};

class ProjectListTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"project.list", "Lista todos los proyectos existentes", R"({"type":"object","properties":{}})"};
    }
    ToolCategory category() const override { return ToolCategory::NONE; }
    ToolResult execute(const nlohmann::json&, const ToolContext&) override {
        return executeCommand("project.list", nlohmann::json::object());
    }
};

class ProjectConfigGetTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{
            "project.config.get", "Obtiene la configuración de un proyecto",
            R"({"type":"object","properties":{"project":{"type":"string"}},"required":["project"]})"
        };
    }
    ToolCategory category() const override { return ToolCategory::NONE; }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("project.config.get", args);
    }
};

} // namespace funshi::agente::llm