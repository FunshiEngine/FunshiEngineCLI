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

class AssetImportTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"asset.import", "Importa un asset",
            R"({"type":"object","properties":{"project":{"type":"string"},"path":{"type":"string"},"type":{"type":"string"}},"required":["project","path","type"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("asset.import", args);
    }
};

class ScriptListTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"script.list", "Lista scripts",
            R"({"type":"object","properties":{"project":{"type":"string"}},"required":["project"]})"};
    }
    ToolCategory category() const override { return ToolCategory::NONE; }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("script.list", args);
    }
};

class ScriptCompileTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"script.compile", "Compila scripts",
            R"({"type":"object","properties":{"project":{"type":"string"}},"required":["project"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("script.compile", args);
    }
};

class DiagnosticsListTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"diagnostics.list", "Lista diagnósticos",
            R"({"type":"object","properties":{"project":{"type":"string"}},"required":["project"]})"};
    }
    ToolCategory category() const override { return ToolCategory::NONE; }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("diagnostics.list", args);
    }
};

class DiagnosticsClearTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override {
        return ToolSpec{"diagnostics.clear", "Limpia diagnósticos",
            R"({"type":"object","properties":{"project":{"type":"string"}},"required":["project"]})"};
    }
    ToolResult execute(const nlohmann::json& args, const ToolContext&) override {
        return executeCommand("diagnostics.clear", args);
    }
};

} // namespace funshi::agente::llm