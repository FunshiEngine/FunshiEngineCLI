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

// AgentLoop: bucle modelo → herramientas → modelo. El saneado de tool calls
// está en ToolCallSanitizer.cpp y el modo prompt en PromptToolParser.cpp.
#include "Agente/LLM/Bucle/AgentLoop.h"
#include <sstream>
#include <algorithm>
#include <regex>

namespace funshi::agente::llm {

AgentLoop::AgentLoop(std::shared_ptr<LlmGateway> gateway,
                     std::shared_ptr<ToolRegistry> registry,
                     bool nativeToolCalls,
                     long toolTimeoutMs)
    : gateway_(std::move(gateway)), registry_(std::move(registry)),
      nativeToolCalls_(nativeToolCalls), toolTimeoutMs_(toolTimeoutMs) {}

std::string AgentLoop::buildSystemPrompt() const {
    std::ostringstream oss;
    oss << "Eres un asistente que controla el motor FunshiEngine. "
        << "Puedes usar herramientas para crear proyectos, escenas, objetos, scripts, etc.\n\n";
    if (!nativeToolCalls_) {
        oss << "HERRAMIENTAS DISPONIBLES:\n";
        for (const auto& spec : registry_->specs()) {
            oss << "- " << spec.name << ": " << spec.description << "\n";
            oss << "  Argumentos JSON: " << spec.parametersSchema << "\n";
        }
        oss << "PARA USAR UNA HERRAMIENTA, respondé SOLO con este bloque exacto:\n"
            << "```tool_call\n{\"tool\":\"nombre\",\"arguments\":{...}}\n```\n"
            << "Cuando recibas el RESULTADO DE LA HERRAMIENTA, respondé al usuario normalmente.\n"
            << "Si NO necesitás una herramienta, respondé normalmente sin ningún bloque.\n";
    }
    return oss.str();
}

AgentAnswer AgentLoop::run(std::vector<ChatMessage>& messages,
                           std::function<void(const AgentEvent&)> onEvent) {
    AgentAnswer answer;
    bool hasSystem = false;
    for (const auto& m : messages) {
        if (m.role == "system") { hasSystem = true; break; }
    }
    if (!hasSystem) {
        messages.insert(messages.begin(), ChatMessage::system(buildSystemPrompt()));
    }

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        std::string accumulatedText;
        std::vector<SanitizedToolCall> pendingNativeCalls;

        auto onChunk = [&](const LlmChunk& chunk) {
            if (chunk.type == LlmChunk::Type::Text) {
                accumulatedText += chunk.delta;
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::Text, chunk.delta});
            } else if (chunk.type == LlmChunk::Type::ToolCall) {
                auto sanitized = ToolCallSanitizer::args(chunk.toolCallName, chunk.toolCallArgsJson);
                if (sanitized.ok()) {
                    pendingNativeCalls.push_back(sanitized);
                } else {
                    accumulatedText += "\n[Error en tool call: " + sanitized.errorReason + "]";
                    if (onEvent) onEvent(AgentEvent{AgentEvent::Type::Text,
                        "\n[Error en tool call: " + sanitized.errorReason + "]"});
                }
            }
        };

        const auto specs = registry_->specs();
        const auto* specsPtr = nativeToolCalls_ ? &specs : nullptr;
        try {
            gateway_->chatStream(messages, specsPtr, onChunk);
        } catch (const LlmException& e) {
            if (e.retryable()) {
                std::string msg = "\n[Error reintentable: " + e.message() + "]";
                accumulatedText += msg;
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::Text, msg});
            } else {
                answer.text += accumulatedText.empty() ? ("Error: " + e.message()) : accumulatedText;
                messages.push_back(ChatMessage::assistant(answer.text));
                return answer;
            }
        }

        if (nativeToolCalls_ && !pendingNativeCalls.empty()) {
            nlohmann::json toolCallsJson = nlohmann::json::array();
            for (const auto& tc : pendingNativeCalls) {
                std::string callId = tc.callId.empty() ? tc.name : tc.callId;
                toolCallsJson.push_back({
                    {"id", callId},
                    {"type", "function"},
                    {"function", {
                        {"name", tc.name},
                        {"arguments", tc.args.dump()}
                    }}
                });
            }
            messages.push_back(ChatMessage::assistantWithTools(accumulatedText, toolCallsJson));

            for (const auto& tc : pendingNativeCalls) {
                std::string callId = tc.callId.empty() ? tc.name : tc.callId;
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::ToolStart, "", callId, tc.name, tc.args.dump()});
                ToolResult result = executeTool(tc.name, tc.args, callId);
                answer.toolCalls.push_back({callId, tc.name, tc.args.dump(), result.ok(), result.render(), {}});
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::ToolEnd, "", callId, tc.name, "", result.ok(), result.render()});
                messages.push_back(ChatMessage::tool(callId, result.render(), tc.name));
            }
            continue;
        }

        if (!nativeToolCalls_) {
            PromptToolParser promptParser;
            std::string visible = promptParser.feed(accumulatedText);
            visible += promptParser.flush();
            auto blocks = promptParser.drain();

            if (!blocks.empty()) {
                std::string toolResultsText;
                messages.push_back(ChatMessage::assistant(accumulatedText));
                bool toolCalled = false;
                for (const auto& block : blocks) {
                    auto sanitized = ToolCallSanitizer::block(block);
                    if (!sanitized.ok()) {
                        std::string err = "\n[Error parseando tool call: " + sanitized.errorReason + "]";
                        answer.text += err;
                        toolResultsText += err + "\n";
                        continue;
                    }
                    toolCalled = true;
                    std::string callId = sanitized.callId.empty() ? sanitized.name : sanitized.callId;
                    if (onEvent) onEvent(AgentEvent{AgentEvent::Type::ToolStart, "", callId, sanitized.name, sanitized.args.dump()});
                    ToolResult result = executeTool(sanitized.name, sanitized.args, callId);
                    answer.toolCalls.push_back({callId, sanitized.name, sanitized.args.dump(), result.ok(), result.render(), {}});
                    if (onEvent) onEvent(AgentEvent{AgentEvent::Type::ToolEnd, "", callId, sanitized.name, "", result.ok(), result.render()});
                    toolResultsText += "RESULTADO DE " + sanitized.name + ":\n" + result.render() + "\n\n";
                }
                if (!visible.empty()) {
                    answer.text += visible;
                }
                if (toolCalled) {
                    messages.push_back(ChatMessage::user(
                        "Resultados de las herramientas ejecutadas:\n" + toolResultsText +
                        "Continúa con la respuesta final para el usuario."));
                    continue;
                }
            }

            std::string finalText = visible.empty() ? accumulatedText : visible;
            answer.text += finalText;
            messages.push_back(ChatMessage::assistant(finalText));
            break;
        }

        answer.text += accumulatedText;
        messages.push_back(ChatMessage::assistant(accumulatedText));
        break;
    }

    return answer;
}

ToolResult AgentLoop::executeTool(const std::string& name, const nlohmann::json& args, const std::string& callId) {
    auto tool = registry_->byName(name);
    if (!tool) return ToolResult::error("Herramienta desconocida: " + name);
    ToolContext ctx; ctx.chatId = callId; ctx.outputsDir = ".agentes/outputs/" + callId;
    try { return tool->execute(args, ctx); }
    catch (const std::exception& e) { return ToolResult::error("Excepción en " + name + ": " + e.what()); }
    catch (...) { return ToolResult::error("Error desconocido en " + name); }
}

std::string AgentLoop::formatToolResult(const ToolResult& result) const { return result.render(); }

} // namespace funshi::agente::llm
