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

// ToolCallSanitizer: repara y valida los argumentos JSON de las tool calls.
#include "Agente/LLM/Bucle/AgentLoop.h"
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
    // Texto entre comillas triples (estilo Python, típico para el código de
    // un archivo): pasa a una cadena JSON con todo escapado.
    for (std::size_t ini; (ini = s.find("\"\"\"")) != std::string::npos;) {
        const std::size_t fin = s.find("\"\"\"", ini + 3);
        if (fin == std::string::npos) break;
        std::string texto = s.substr(ini + 3, fin - ini - 3);
        if (!texto.empty() && texto.front() == '\n') texto.erase(0, 1);
        s.replace(ini, fin + 3 - ini, nlohmann::json(texto).dump());
    }
    s = std::regex_replace(s, std::regex(R"(,\s*([}\]]))"), "$1");
    s = std::regex_replace(s, std::regex(R"(: NaN)"), ": 0");
    s = std::regex_replace(s, std::regex(R"(: Infinite)"), ": 0");
    // Nombre y argumentos encadenados sin separar (Phi-4-mini):
    // {"tool_call": "nombre": {...}} → {"tool":"nombre","arguments":{...}}.
    s = std::regex_replace(
        s, std::regex(R"re("(tool_call|tool|name|function|herramienta)"\s*:\s*"([A-Za-z0-9_.]+)"\s*:\s*([\{\[]))re"),
        "\"tool\":\"$2\",\"arguments\":$3");
    // Llaves y corchetes desparejos (modelos chicos: "[{..},{..}}}" o falta
    // la última llave). Se reconstruye: un cierre que no corresponde con la
    // apertura pendiente cierra antes lo pendiente; los cierres de más se
    // descartan; al final se cierra lo que quedó abierto.
    std::string reconstruido;
    std::string pila;
    bool enCadena = false, escape = false;
    for (char c : s) {
        if (enCadena) {
            // Saltos de línea y tabulaciones crudos dentro de una cadena (código
            // escrito tal cual): JSON los exige escapados.
            if (!escape && (c == '\n' || c == '\t' || c == '\r')) {
                if (c != '\r') reconstruido += (c == '\n') ? "\\n" : "\\t";
                continue;
            }
            reconstruido += c;
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') enCadena = false;
            continue;
        }
        if (c == '"') {
            enCadena = true;
        } else if (c == '{' || c == '[') {
            pila += c;
        } else if (c == '}' || c == ']') {
            const char apertura = (c == '}') ? '{' : '[';
            if (pila.find(apertura) == std::string::npos) continue;  // cierre de más
            while (!pila.empty() && pila.back() != apertura) {
                reconstruido += (pila.back() == '{') ? '}' : ']';
                pila.pop_back();
            }
            pila.pop_back();
        }
        reconstruido += c;
    }
    if (enCadena) reconstruido += '"';
    for (auto it = pila.rbegin(); it != pila.rend(); ++it) reconstruido += (*it == '{') ? '}' : ']';
    s = std::move(reconstruido);
    return s;
}

SanitizedToolCall ToolCallSanitizer::validateShape(const nlohmann::json& obj, const std::string* expectedName) {
    if (!obj.is_object()) return SanitizedToolCall::invalid("No es un objeto JSON");
    // Llamada envuelta: {"tool_call": {"tool": …, "arguments": …}}.
    for (const char* envoltorio : {"tool_call", "function_call", "call"}) {
        if (obj.size() == 1 && obj.contains(envoltorio) && obj[envoltorio].is_object()) {
            return validateShape(obj[envoltorio], expectedName);
        }
    }
    std::string name;
    // Claves que usan los modelos para el nombre (la pedida es "tool").
    for (const char* clave : {"tool", "name", "tool_call", "function", "herramienta"}) {
        if (obj.contains(clave) && obj[clave].is_string()) {
            name = obj[clave].get<std::string>();
            break;
        }
    }
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
        // El formato de los argumentos de ejecutar_comando escrito como si
        // fuera la llamada: {"comando": "x", "argumentos": {...}}.
        if (obj.contains("comando") && obj["comando"].is_string()) {
            nlohmann::json args = {{"comando", obj["comando"]}, {"argumentos", nlohmann::json::object()}};
            for (const char* clave : {"argumentos", "arguments", "args", "parameters", "parametros"}) {
                if (obj.contains(clave) && obj[clave].is_object()) {
                    args["argumentos"] = obj[clave];
                    break;
                }
            }
            return SanitizedToolCall::valid("ejecutar_comando", args, "ejecutar_comando");
        }
        return SanitizedToolCall::invalid("Falta el campo \"tool\" con el nombre de la herramienta.");
    }
    nlohmann::json args = nlohmann::json::object();
    if (obj.contains("arguments") && obj["arguments"].is_object()) args = obj["arguments"];
    else if (obj.contains("args") && obj["args"].is_object()) args = obj["args"];
    else if (obj.contains("parameters") && obj["parameters"].is_object()) args = obj["parameters"];
    else {
        for (auto& [k, v] : obj.items()) {
            if (k != "tool" && k != "name" && k != "tool_call" && k != "function" && k != "herramienta" &&
                k != "arguments" && k != "args") {
                args[k] = v;
            }
        }
    }
    return SanitizedToolCall::valid(name, args, name);
}

} // namespace funshi::agente::llm
