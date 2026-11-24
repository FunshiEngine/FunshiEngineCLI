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

// PromptToolParser: extrae bloques ```tool_call``` del texto en modo prompt.
#include "Agente/LLM/Bucle/AgentLoop.h"
#include <sstream>
#include <algorithm>
#include <regex>

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
    // La última línea suele llegar sin '\n' (p. ej. el ``` de cierre al final
    // del stream): se procesa como una línea completa.
    std::string tail;
    if (!pendingLine_.empty()) {
        tail = consumeLine(pendingLine_);
        pendingLine_.clear();
    }
    // Bloque sin cierre: los modelos pequeños a veces omiten el ```. Si hay
    // contenido, se intenta igual (el sanitizador valida el JSON).
    if (inTool_) {
        inTool_ = false;
        if (toolBuf_.find_first_not_of(" \t\r\n") != std::string::npos) {
            completed_.push_back(toolBuf_);
        }
        toolBuf_.clear();
    }
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

std::vector<std::string> PromptToolParser::llamadasSinCerco(const std::string& texto) {
    std::vector<std::string> llamadas;
    std::size_t desde = 0;
    while ((desde = texto.find('{', desde)) != std::string::npos) {
        int profundidad = 0;
        bool enCadena = false, escape = false;
        std::size_t fin = std::string::npos;
        for (std::size_t i = desde; i < texto.size(); ++i) {
            const char c = texto[i];
            if (enCadena) {
                if (escape) escape = false;
                else if (c == '\\') escape = true;
                else if (c == '"') enCadena = false;
                continue;
            }
            if (c == '"') enCadena = true;
            else if (c == '{') ++profundidad;
            else if (c == '}' && --profundidad == 0) { fin = i; break; }
        }
        if (fin == std::string::npos) break;
        llamadas.push_back(texto.substr(desde, fin - desde + 1));
        desde = fin + 1;
    }
    return llamadas;
}

} // namespace funshi::agente::llm
