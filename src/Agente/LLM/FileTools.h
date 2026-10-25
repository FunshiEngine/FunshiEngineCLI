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

class FileCreateTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"file.create", "Crea un archivo",
            R"({"type":"object","properties":{"project":{"type":"string"},"path":{"type":"string"},"content":{"type":"string"}},"required":["project","path","content"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("file.create", args);
    }
};

class DirImportTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"dir.import", "Importa un directorio",
            R"({"type":"object","properties":{"project":{"type":"string"},"source":{"type":"string"},"destination":{"type":"string"}},"required":["project","source","destination"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("dir.import", args);
    }
};

class FsCreateTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"fs.create", "Crea un directorio",
            R"({"type":"object","properties":{"project":{"type":"string"},"path":{"type":"string"}},"required":["project","path"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("fs.create", args);
    }
};

class ProjectSrcCreateTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"project.src.create", "Crea estructura src/",
            R"({"type":"object","properties":{"project":{"type":"string"}},"required":["project"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("project.src.create", args);
    }
};

} // namespace funshi::agente::llm