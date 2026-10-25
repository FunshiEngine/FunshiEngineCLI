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

#include "LlmTypes.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>

namespace funshi::agente::llm {

/**
 * Categoría de permiso de una herramienta.
 */
enum class ToolCategory {
    NONE,   // Sin efecto externo (ej. get_time)
    FILES,  // Escribir archivos / descargar
    WEB,    // Consultar web
    MCP     // Herramientas de servidores MCP
};

/**
 * Origen de la herramienta.
 */
enum class ToolOrigin {
    LOCAL,
    MCP
};

/**
 * Decisión de permiso del usuario.
 */
enum class PermissionDecision {
    ALLOW_ONCE,
    ALLOW_ALWAYS,
    DENY
};

/**
 * Petición de permiso para la UI.
 */
struct PermissionRequest {
    std::string chatId;
    std::string tool;
    ToolCategory category;
    std::string summary;
    std::string argsJson;
};

/**
 * Contexto de ejecución de una herramienta.
 */
struct ToolContext {
    std::string chatId;
    std::string outputsDir;                 // Carpeta donde escribir archivos
    std::vector<ToolCategory> grantedCategories; // Categorías ya concedidas
    std::function<PermissionDecision(const PermissionRequest&)> requestPermission = [](const auto&) { return PermissionDecision::DENY; };
};

/**
 * Archivo producido por una herramienta (adjunto en el chat).
 */
struct Artifact {
    std::string name;
    std::string path;
    long sizeBytes = 0;
    std::string mime;
};

/**
 * Resultado de ejecutar una herramienta.
 */
struct ToolResult {
    enum class Type { OK, ERROR } type;
    std::string text;                       // Texto para devolver al modelo
    std::vector<Artifact> artifacts;        // Archivos producidos
    std::string errorMessage;               // Solo si type == ERROR

    static ToolResult ok(std::string text, std::vector<Artifact> artifacts = {}) {
        ToolResult r; r.type = Type::OK; r.text = std::move(text); r.artifacts = std::move(artifacts); return r;
    }
    static ToolResult error(std::string msg) {
        ToolResult r; r.type = Type::ERROR; r.errorMessage = std::move(msg); return r;
    }

    bool ok() const { return type == Type::OK; }
    std::string render() const { return ok() ? text : errorMessage; }
};

/**
 * Interfaz base de una herramienta que el modelo puede pedir.
 */
class Tool {
public:
    virtual ~Tool() = default;

    virtual ToolSpec spec() const = 0;
    virtual ToolCategory category() const { return ToolCategory::NONE; }
    virtual ToolOrigin origin() const { return ToolOrigin::LOCAL; }
    virtual std::string describeCall(const nlohmann::json& args) const { return spec().name; }

    // Ejecuta la herramienta. Todo I/O bloqueante va en hilo separado si hace falta.
    virtual ToolResult execute(const nlohmann::json& args, const ToolContext& ctx) = 0;
};

/**
 * Registro mutable de herramientas disponibles.
 */
class ToolRegistry {
public:
    void add(std::shared_ptr<Tool> tool) {
        tools_.push_back(std::move(tool));
    }
    void addAll(std::vector<std::shared_ptr<Tool>> tools) {
        for (auto& t : tools) add(std::move(t));
    }

    std::shared_ptr<Tool> byName(const std::string& name) const {
        for (const auto& t : tools_) if (t->spec().name == name) return t;
        return nullptr;
    }

    std::vector<ToolSpec> specs() const {
        std::vector<ToolSpec> out;
        for (const auto& t : tools_) out.push_back(t->spec());
        return out;
    }

    std::vector<std::shared_ptr<Tool>> all() const { return tools_; }
    bool empty() const { return tools_.empty(); }

    std::string describe() const {
        std::string out;
        for (const auto& t : tools_) {
            out += "· `" + t->spec().name + "` — " + t->spec().description + "\n";
        }
        return out;
    }

    nlohmann::json parseArgs(const std::string& argsJson) const {
        try {
            if (argsJson.empty() || argsJson == "{}") return nlohmann::json::object();
            return nlohmann::json::parse(argsJson);
        } catch (...) {
            return nlohmann::json::object();
        }
    }

private:
    std::vector<std::shared_ptr<Tool>> tools_;
};

} // namespace funshi::agente::llm