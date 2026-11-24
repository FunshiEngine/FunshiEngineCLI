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

// AgentLoop — herramientas: ejecución, alias, nombres mal escritos y
// llamadas sin nombre.
#include "Agente/LLM/Bucle/AgentLoop.h"

#include <cstring>

#include <algorithm>
#include <vector>

namespace funshi::agente::llm {

// Mensajes del modo prompt.

std::string resultadosParaModelo(const std::string& resultados) {
    return std::string(kInicioSalida) + " — mensaje automático, no lo escribió el usuario]\n"
           "Ejecutaste estas herramientas y obtuviste:\n\n" +
           resultados + kFinSalida +
           "\nSi el pedido ya está cumplido, responde ahora al usuario sin más herramientas.";
}

std::string sinMarcasDeSalida(const std::string& texto) {
    // Los modelos chicos copian las marcas del mensaje de resultados en su respuesta.
    std::string out;
    std::size_t ini = 0;
    bool anteriorFueResultado = false;
    while (ini <= texto.size()) {
        auto fin = texto.find('\n', ini);
        if (fin == std::string::npos) fin = texto.size();
        const std::string linea = texto.substr(ini, fin - ini);
        const auto t = linea.find_first_not_of(" \t\r");
        const auto empieza = [&](const char* prefijo) {
            return t != std::string::npos && linea.compare(t, std::strlen(prefijo), prefijo) == 0;
        };
        // "RESULTADO DE x:" y el JSON que lo sigue son el resultado copiado.
        const bool copiado = empieza(kResultadoDe) || (anteriorFueResultado && empieza("{"));
        anteriorFueResultado = empieza(kResultadoDe) || (anteriorFueResultado && empieza("{"));
        if (!copiado && !empieza(kFinSalida) && !empieza(kInicioSalida)) out += linea + (fin < texto.size() ? "\n" : "");
        ini = fin + 1;
    }
    const auto ultimo = out.find_last_not_of(" \t\r\n");
    return ultimo == std::string::npos ? std::string() : out.substr(0, ultimo + 1);
}

namespace detalle {

// Respuesta que es (casi) solo un objeto JSON sin nombre de herramienta: el
// modelo escribió los argumentos y olvidó el nombre. Se infiere la
// herramienta por sus campos obligatorios, solo entre las meta-herramientas
// (un JSON explicativo nunca dispara un comando del motor).
std::string inferirLlamadaSinNombre(const std::string& texto, const ToolRegistry& registro) {
    const auto ini = texto.find('{');
    const auto fin = texto.rfind('}');
    if (ini == std::string::npos || fin == std::string::npos || fin <= ini) return {};
    if (texto.size() - (fin - ini + 1) > 200) return {};  // hay mucha prosa alrededor
    const auto obj = nlohmann::json::parse(texto.substr(ini, fin - ini + 1), nullptr, false);
    if (!obj.is_object() || obj.contains("tool") || obj.contains("name")) return {};
    for (const char* candidata : {"lanzar_subagentes", "ejecutar_comando", "buscar_comandos"}) {
        const auto tool = registro.byName(candidata);
        if (!tool) continue;
        const auto esquema = nlohmann::json::parse(tool->spec().parametersSchema, nullptr, false);
        const auto requeridos = esquema.is_object() ? esquema.value("required", nlohmann::json::array())
                                                    : nlohmann::json::array();
        bool todos = !requeridos.empty();
        for (const auto& r : requeridos) todos = todos && r.is_string() && obj.contains(r.get<std::string>());
        if (todos) return nlohmann::json{{"tool", candidata}, {"arguments", obj}}.dump();
    }
    return {};
}

}  // namespace detalle

namespace {

// Distancia de edición (Levenshtein) para nombres de herramienta mal escritos.
std::size_t distancia(const std::string& a, const std::string& b) {
    std::vector<std::size_t> fila(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) fila[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = fila[0];
        fila[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t arriba = fila[j];
            fila[j] = std::min({fila[j] + 1, fila[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = arriba;
        }
    }
    return fila[b.size()];
}

}  // namespace

ToolResult AgentLoop::executeTool(const std::string& name, const nlohmann::json& args, const std::string& callId) {
    auto tool = registry_->byName(name);
    nlohmann::json argsReales = args;
    if (!tool && alias_) {
        std::string nombreReal;
        if (alias_(name, args, nombreReal, argsReales)) tool = registry_->byName(nombreReal);
    }
    if (!tool) tool = registry_->byName(nombreParecido(name));
    if (!tool) {
        return ToolResult::error("Herramienta desconocida: " + name +
                                 ". Usa solo las herramientas de la lista (o ejecutar_comando).");
    }
    ToolContext ctx; ctx.chatId = callId; ctx.outputsDir = ".agentes/outputs/" + callId;
    ctx.onEvent = onEventActual_;
    try { return tool->execute(argsReales, ctx); }
    catch (const std::exception& e) { return ToolResult::error("Excepción en " + name + ": " + e.what()); }
    catch (...) { return ToolResult::error("Error desconocido en " + name); }
}

// Herramienta registrada cuyo nombre está a poca distancia de `nombre`
// ("lancer_subagents" → "lanzar_subagentes"); vacío si ninguna es clara.
std::string AgentLoop::nombreParecido(const std::string& nombre) const {
    std::string mejor;
    std::size_t mejorDist = std::string::npos;
    for (const auto& spec : registry_->specs()) {
        const std::size_t d = distancia(nombre, spec.name);
        if (d < mejorDist) {
            mejorDist = d;
            mejor = spec.name;
        }
    }
    const std::size_t tope = std::max<std::size_t>(2, mejor.size() / 5);
    return mejorDist <= tope ? mejor : std::string();
}

bool AgentLoop::existeHerramienta(const std::string& nombre) const {
    if (registry_->byName(nombre) || !nombreParecido(nombre).empty()) return true;
    std::string real;
    nlohmann::json args;
    return alias_ && alias_(nombre, nlohmann::json::object(), real, args) && registry_->byName(real);
}

} // namespace funshi::agente::llm
