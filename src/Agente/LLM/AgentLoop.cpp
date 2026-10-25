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

#include "AgentLoop.h"
#include <sstream>
#include <algorithm>
#include <regex>

namespace funshi::agente::llm {

// ============================================================
// ToolCallSanitizer implementation
// ============================================================

nlohmann::json ToolCallSanitizer::lenientJson = nlohmann::json::object();

SanitizedToolCall ToolCallSanitizer::args(const std::string& expectedName, const std::string& argsJson) {
    std::string slide = cutToOuterBraces(argsJson.empty() ? "{}" : argsJson);
    if (slide.empty()) return SanitizedToolCall::invalid("Los argumentos no contienen un objeto JSON.");
    auto outcome = parseAndValidate(slide, &expectedName);
    if (outcome.ok()) return outcome;
    try {
        auto onlyArgs = lenientJson.parse(slide);
        if (onlyArgs.is_object() && onlyArgs.contains("arguments") && onlyArgs["arguments"].is_object()) {
            return SanitizedToolCall::valid(expectedName, onlyArgs["arguments"], expectedName);
        }
    } catch (...) {}
    return outcome;
}

SanitizedToolCall ToolCallSanitizer::block(const std::string& raw) {
    std::string cleaned = stripWrappers(raw);
    std::string slide = cutToOuterBraces(cleaned);
    if (slide.empty()) return SanitizedToolCall::invalid("No se encontró un objeto JSON en el bloque.");
    return parseAndValidate(slide);
}

SanitizedToolCall ToolCallSanitizer::parseAndValidate(const std::string& raw, const std::string* expectedName) {
    nlohmann::json direct;
    try { direct = nlohmann::json::parse(raw, nullptr, false); } catch (...) {}
    if (!direct.is_discarded()) {
        auto shaped = validateShape(direct, expectedName);
        if (shaped.ok()) return shaped;
    }
    std::string repaired = repairJson(raw);
    if (repaired != raw) {
        try {
            auto parsed = nlohmann::json::parse(repaired, nullptr, false);
            if (!parsed.is_discarded()) {
                auto reshaped = validateShape(parsed, expectedName);
                if (reshaped.ok()) return reshaped;
            }
        } catch (...) {}
    }
    return SanitizedToolCall::invalid("JSON inválido incluso tras repararlo. Ejemplo: {\"tool\":\"nombre\",\"arguments\":{...}}");
}

std::string ToolCallSanitizer::stripWrappers(const std::string& raw) {
    std::string s = raw;
    std::regex fence_start(R"(^\s*```+[a-zA-Z_]*\s*)");
    std::regex fence_end(R"(```+\s*$)");
    s = std::regex_replace(s, fence_start, "");
    s = std::regex_replace(s, fence_end, "");
    return s;
}

std::string ToolCallSanitizer::cutToOuterBraces(const std::string& raw) {
    size_t start = raw.find('{');
    size_t end = raw.rfind('}');
    if (start == std::string::npos || end <= start || end >= raw.size()) return "";
    return raw.substr(start, end - start + 1);
}

std::string ToolCallSanitizer::repairJson(const std::string& raw) {
    std::string s = raw;
    if (!s.empty() && static_cast<unsigned char>(s[0]) == 0xEF && s.size() >= 3 &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    // Replace smart quotes (UTF-8 multi-byte) with ASCII
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xE2 && i + 2 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i+1]);
            unsigned char c2 = static_cast<unsigned char>(s[i+2]);
            if (c1 == 0x80 && (c2 == 0x9C || c2 == 0x9D)) {
                out += '"';
                i += 2;
                continue;
            }
            if (c1 == 0x80 && (c2 == 0x98 || c2 == 0x99)) {
                out += '\'';
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    s = std::move(out);
    s = std::regex_replace(s, std::regex(R"(,\s*([}\]]))"), "$1");
    s = std::regex_replace(s, std::regex(R"(: NaN)"), ": 0");
    s = std::regex_replace(s, std::regex(R"(: Infinite)"), ": 0");
    return s;
}

SanitizedToolCall ToolCallSanitizer::validateShape(const nlohmann::json& obj, const std::string* expectedName) {
    if (!obj.is_object()) return SanitizedToolCall::invalid("No es un objeto JSON");
    std::string name;
    if (obj.contains("tool") && obj["tool"].is_string()) name = obj["tool"].get<std::string>();
    else if (obj.contains("name") && obj["name"].is_string()) name = obj["name"].get<std::string>();
    if (name.empty()) {
        if (expectedName && !expectedName->empty()) {
            if (obj.contains("arguments") && obj["arguments"].is_object()) {
                return SanitizedToolCall::valid(*expectedName, obj["arguments"], *expectedName);
            }
            nlohmann::json args = nlohmann::json::object();
            for (auto& [k, v] : obj.items()) {
                if (k != "tool" && k != "name" && k != "arguments" && k != "args") args[k] = v;
            }
            return SanitizedToolCall::valid(*expectedName, args, *expectedName);
        }
        return SanitizedToolCall::invalid("Falta el campo \"tool\" con el nombre de la herramienta.");
    }
    nlohmann::json args = nlohmann::json::object();
    if (obj.contains("arguments") && obj["arguments"].is_object()) args = obj["arguments"];
    else if (obj.contains("args") && obj["args"].is_object()) args = obj["args"];
    else {
        for (auto& [k, v] : obj.items()) {
            if (k != "tool" && k != "name" && k != "arguments" && k != "args") args[k] = v;
        }
    }
    return SanitizedToolCall::valid(name, args, name);
}

} // namespace funshi::agente::llm

namespace funshi::agente::llm {

// ============================================================
// PromptToolParser
// ============================================================

std::string PromptToolParser::feed(const std::string& delta) {
    std::string out;
    for (char ch : delta) {
        if (ch != '\n') { pendingLine_ += ch; continue; }
        pendingLine_ += ch;
        out += consumeLine(pendingLine_);
        pendingLine_.clear();
    }
    if (!inTool_ && !pendingLine_.empty()) {
        std::string t = pendingLine_;
        t.erase(0, t.find_first_not_of(" \t\r\n"));
        t.erase(t.find_last_not_of(" \t\r\n") + 1);
        const std::string opener = OPENER;
        if (!(t.size() >= opener.size() && t.compare(0, opener.size(), opener) == 0)) {
            out += pendingLine_;
            pendingLine_.clear();
        }
    }
    return out;
}

std::string PromptToolParser::flush() {
    std::string tail = pendingLine_;
    pendingLine_.clear();
    return tail;
}

std::vector<std::string> PromptToolParser::drain() {
    std::vector<std::string> out = completed_;
    completed_.clear();
    return out;
}

std::string PromptToolParser::consumeLine(const std::string& line) {
    std::string t = line;
    t.erase(0, t.find_first_not_of(" \t\r\n"));
    t.erase(t.find_last_not_of(" \t\r\n") + 1);
    if (inTool_) {
        if (t == "```") { inTool_ = false; completed_.push_back(toolBuf_); toolBuf_.clear(); return ""; }
        toolBuf_ += line; return "";
    }
    if (t == OPENER) { inTool_ = true; return ""; }
    return line;
}

} // namespace funshi::agente::llm

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

AgentAnswer AgentLoop::run(const std::vector<ChatMessage>& messages,
                           std::function<void(const AgentEvent&)> onEvent) {
    AgentAnswer answer;
    std::vector<ChatMessage> currentMessages = messages;
    bool hasSystem = false;
    for (const auto& m : currentMessages) if (m.role == "system") { hasSystem = true; break; }
    if (!hasSystem) currentMessages.insert(currentMessages.begin(), ChatMessage::system(buildSystemPrompt()));

    PromptToolParser promptParser;

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        bool toolCalled = false;
        std::string accumulatedText;
        std::vector<SanitizedToolCall> pendingNativeCalls;

        auto onChunk = [&](const LlmChunk& chunk) {
            if (chunk.type == LlmChunk::Type::Text) {
                accumulatedText += chunk.delta;
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::Text, chunk.delta});
            } else if (chunk.type == LlmChunk::Type::ToolCall) {
                auto sanitized = ToolCallSanitizer::args(chunk.toolCallName, chunk.toolCallArgsJson);
                if (sanitized.ok()) pendingNativeCalls.push_back(sanitized);
                else accumulatedText += "\n[Error en tool call: " + sanitized.errorReason + "]";
            }
        };

        const auto specs = registry_->specs();
        const auto* specsPtr = nativeToolCalls_ ? &specs : nullptr;
        try { gateway_->chatStream(currentMessages, specsPtr, onChunk); }
        catch (const LlmException& e) {
            if (e.retryable()) accumulatedText += "\n[Error reintentable: " + e.message() + "]";
            else { answer.text = accumulatedText.empty() ? "Error: " + e.message() : accumulatedText; return answer; }
        }

        if (nativeToolCalls_ && !pendingNativeCalls.empty()) {
            for (const auto& tc : pendingNativeCalls) {
                toolCalled = true;
                std::string callId = tc.callId.empty() ? tc.name : tc.callId;
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::ToolStart, "", callId, tc.name, tc.args.dump()});
                ToolResult result = executeTool(tc.name, tc.args, callId);
                answer.toolCalls.push_back({callId, tc.name, tc.args.dump(), result.ok(), result.render(), {}});
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::ToolEnd, "", callId, tc.name, "", result.ok(), result.render()});
                currentMessages.push_back(ChatMessage::tool(callId, result.render(), tc.name));
            }
        } else if (!nativeToolCalls_) {
            std::string visible = promptParser.feed(accumulatedText);
            auto blocks = promptParser.drain();
            for (const auto& block : blocks) {
                auto sanitized = ToolCallSanitizer::block(block);
                if (!sanitized.ok()) { accumulatedText += "\n[Error parseando tool call: " + sanitized.errorReason + "]"; continue; }
                toolCalled = true;
                std::string callId = sanitized.callId.empty() ? sanitized.name : sanitized.callId;
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::ToolStart, "", callId, sanitized.name, sanitized.args.dump()});
                ToolResult result = executeTool(sanitized.name, sanitized.args, callId);
                answer.toolCalls.push_back({callId, sanitized.name, sanitized.args.dump(), result.ok(), result.render(), {}});
                if (onEvent) onEvent(AgentEvent{AgentEvent::Type::ToolEnd, "", callId, sanitized.name, "", result.ok(), result.render()});
                currentMessages.push_back(ChatMessage::tool(callId, result.render(), sanitized.name));
            }
            answer.text += visible;
        } else { answer.text += accumulatedText; }

        if (!toolCalled) { answer.text += accumulatedText; break; }
    }

    if (!nativeToolCalls_) answer.text += promptParser.flush();
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