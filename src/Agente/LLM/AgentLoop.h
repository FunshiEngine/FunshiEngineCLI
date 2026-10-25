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

#include "LlmGateway.h"
#include "ToolRegistry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>

namespace funshi::agente::llm {

/**
 * Sanitizador de tool calls: repara JSON roto del modelo.
 */
struct SanitizedToolCall {
    enum class Type { VALID, INVALID } type;
    std::string name;
    nlohmann::json args;
    std::string callId;
    std::string errorReason;

    static SanitizedToolCall valid(std::string name, nlohmann::json args, std::string callId) {
        SanitizedToolCall r; r.type = Type::VALID; r.name = std::move(name); r.args = std::move(args); r.callId = std::move(callId); return r;
    }
    static SanitizedToolCall invalid(std::string reason) {
        SanitizedToolCall r; r.type = Type::INVALID; r.errorReason = std::move(reason); return r;
    }
    bool ok() const { return type == Type::VALID; }
};

class ToolCallSanitizer {
public:
    // Para modo nativo (tool_calls del stream OpenAI)
    static SanitizedToolCall args(const std::string& expectedName, const std::string& argsJson);

    // Para modo prompt (bloques ```tool_call ... ```)
    static SanitizedToolCall block(const std::string& raw);

private:
    static nlohmann::json lenientJson;
    static SanitizedToolCall parseAndValidate(const std::string& raw, const std::string* expectedName = nullptr);
    static std::string stripWrappers(const std::string& raw);
    static std::string cutToOuterBraces(const std::string& raw);
    static std::string repairJson(const std::string& raw);
    static SanitizedToolCall validateShape(const nlohmann::json& obj, const std::string* expectedName);
};

/**
 * Parser incremental para modo prompt (línea a línea).
 */
class PromptToolParser {
public:
    // Procesa un delta y devuelve texto visible para mostrar ya
    std::string feed(const std::string& delta);

    // Texto retenido al terminar el stream
    std::string flush();

    // Bloques completos desde la última lectura
    std::vector<std::string> drain();

private:
    std::string pendingLine_;
    bool inTool_ = false;
    std::string toolBuf_;
    std::vector<std::string> completed_;
    static constexpr const char* OPENER = "```tool_call";
    std::string consumeLine(const std::string& line);
};

/**
 * Bucle principal: modelo → tool → modelo → tool...
 */
class AgentLoop {
public:
    AgentLoop(std::shared_ptr<LlmGateway> gateway,
              std::shared_ptr<ToolRegistry> registry,
              bool nativeToolCalls = true,
              long toolTimeoutMs = 30000);

    // Máximo de rondas modelo+herramienta
    int maxIterations = 8;

    /**
     * Ejecuta el loop con callback de eventos para UI en vivo.
     */
    AgentAnswer run(const std::vector<ChatMessage>& messages,
                    std::function<void(const AgentEvent&)> onEvent = nullptr);

    // Config
    void setNativeToolCalls(bool v) { nativeToolCalls_ = v; }
    void setMaxIterations(int v) { maxIterations = v; }

private:
    std::shared_ptr<LlmGateway> gateway_;
    std::shared_ptr<ToolRegistry> registry_;
    bool nativeToolCalls_;
    long toolTimeoutMs_;

    std::string buildSystemPrompt() const;
    ToolResult executeTool(const std::string& name, const nlohmann::json& args, const std::string& callId);
    std::string formatToolResult(const ToolResult& result) const;
};

} // namespace funshi::agente::llm