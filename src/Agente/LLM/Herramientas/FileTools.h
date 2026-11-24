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

class FileCreateTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"file.create", "Crea un archivo dentro del vfs fs del proyecto",
            R"({"type":"object","properties":{"project":{"type":"string"},"name":{"type":"string"},"path":{"type":"string"},"content":{"type":"string"},"resumen":{"type":"string","description":"Una frase: qué contiene y hace el archivo"}},"required":["project","name"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("file.create", args);
    }
};

class DirImportTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"project.dir.import", "Importa recursivamente un directorio dentro de src/<proyecto>",
            R"({"type":"object","properties":{"project":{"type":"string"},"source":{"type":"string"},"path":{"type":"string"},"overwrite":{"type":"boolean"}},"required":["project","source","path"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("project.dir.import", args);
    }
};

class FsCreateTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"project.fs.create", "Crea un directorio dentro del vfs fs del proyecto",
            R"({"type":"object","properties":{"project":{"type":"string"},"name":{"type":"string"}},"required":["project","name"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("project.fs.create", args);
    }
};

class ProjectSrcCreateTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"project.src.create", "Crea un archivo o directorio dentro de src/<proyecto>",
            R"({"type":"object","properties":{"project":{"type":"string"},"path":{"type":"string"},"file":{"type":"boolean"},"content":{"type":"string"},"resumen":{"type":"string","description":"Una frase: qué contiene y hace el archivo"}},"required":["project","path"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("project.src.create", args);
    }
};

} // namespace funshi::agente::llm