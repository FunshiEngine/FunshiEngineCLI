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

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace funshi::agente::llm {

/**
 * Definición de una herramienta para el modelo (formato OpenAI):
 * nombre, descripción y JSON Schema de los argumentos.
 */
struct ToolSpec {
    std::string name;
    std::string description;
    std::string parametersSchema = R"({"type":"object","properties":{}})";

    nlohmann::json toJson() const {
        return {
            {"type", "function"},
            {"function", {
                {"name", name},
                {"description", description},
                {"parameters", nlohmann::json::parse(parametersSchema)}
            }}
        };
    }
};

/**
 * Chunk tipado del streaming del modelo: texto o pedido de herramienta.
 */
struct LlmChunk {
    enum class Type { Text, ToolCall } type;
    std::string delta;           // Para Text: fragmento de texto
    std::string toolCallId;      // Para ToolCall: id de la llamada
    std::string toolCallName;    // Para ToolCall: nombre de la herramienta
    std::string toolCallArgsJson; // Para ToolCall: argumentos como JSON crudo

    static LlmChunk makeText(std::string d) {
        LlmChunk c; c.type = Type::Text; c.delta = std::move(d); return c;
    }
    static LlmChunk makeToolCall(std::string id, std::string name, std::string argsJson) {
        LlmChunk c; c.type = Type::ToolCall;
        c.toolCallId = std::move(id);
        c.toolCallName = std::move(name);
        c.toolCallArgsJson = std::move(argsJson);
        return c;
    }
};

/**
 * Mensaje de chat (formato OpenAI compatible).
 */
struct ChatMessage {
    std::string role;                    // "system" | "user" | "assistant" | "tool"
    std::string content;                 // Texto del mensaje
    std::optional<nlohmann::json> toolCalls; // Para role=assistant con tool_calls
    std::optional<std::string> toolCallId;   // Para role=tool: id del tool call respondido
    std::optional<std::string> toolName;     // Para role=tool: nombre de la herramienta (algunos proveedores)

    nlohmann::json toJson() const {
        nlohmann::json j = {
            {"role", role},
            {"content", content}
        };
        if (toolCalls) j["tool_calls"] = *toolCalls;
        if (toolCallId) j["tool_call_id"] = *toolCallId;
        if (toolName) j["name"] = *toolName;
        return j;
    }

    static ChatMessage system(std::string content) {
        return {"system", std::move(content)};
    }
    static ChatMessage user(std::string content) {
        return {"user", std::move(content)};
    }
    static ChatMessage assistant(std::string content) {
        return {"assistant", std::move(content)};
    }
    static ChatMessage assistantWithTools(std::string content, nlohmann::json toolCalls) {
        ChatMessage m = assistant(std::move(content));
        m.toolCalls = std::move(toolCalls);
        return m;
    }
    static ChatMessage tool(std::string toolCallId, std::string result, std::string toolName = "") {
        ChatMessage m = {"tool", std::move(result), std::nullopt, std::move(toolCallId), std::move(toolName)};
        return m;
    }
};

/**
 * Respuesta completa del loop: texto visible + herramientas usadas.
 */
struct AgentAnswer {
    std::string text;
    struct ExecutedTool {
        std::string callId;
        std::string name;
        std::string argsJson;
        bool ok;
        std::string result;
        std::vector<std::string> artifacts; // Rutas de archivos producidos
    };
    std::vector<ExecutedTool> toolCalls;
};

/**
 * Eventos del loop para la UI (texto en vivo y tarjetas de herramientas).
 */
struct AgentEvent {
    enum class Type { Text, ToolStart, ToolEnd } type;
    std::string delta;           // Text
    std::string callId;          // ToolStart/ToolEnd
    std::string name;            // ToolStart/ToolEnd
    std::string argsJson;        // ToolStart
    bool ok = false;             // ToolEnd
    std::string result;          // ToolEnd
    std::vector<std::string> artifacts; // ToolEnd
};

} // namespace funshi::agente::llm