/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Tareas/TareaAgente.h"

#include "Agente/LLM/Cliente/LlmGateway.h"

#include <regex>

namespace funshi::agente::tareas {

namespace {

constexpr std::size_t kMaxLineas = 8;        // acciones de "Lo que hice"
constexpr std::size_t kMaxLinea = 220;
constexpr std::size_t kMaxResumen = 400;
constexpr std::size_t kMaxRespuesta = 400;   // respuesta final en el pedido de cierre

std::string recortar(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t corte = max;
    while (corte > 0 && (static_cast<unsigned char>(s[corte]) & 0xC0) == 0x80) --corte;  // no partir UTF-8
    return s.substr(0, corte) + "…";
}

std::string sinEspacios(const std::string& s) {
    const auto ini = s.find_first_not_of(" \t\r\n");
    if (ini == std::string::npos) return {};
    return s.substr(ini, s.find_last_not_of(" \t\r\n") - ini + 1);
}

// Comillas y negritas con las que los modelos adornan la línea.
std::string limpiarLinea(std::string s) {
    const auto adorno = [](char c) { return c == '"' || c == '*' || c == '`' || c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && adorno(s.front())) s.erase(0, 1);
    while (!s.empty() && adorno(s.back())) s.pop_back();
    return s;
}

}  // namespace

nlohmann::json TareaAgente::aJson() const {
    return {{"pedido", pedido}, {"objetivo", objetivo}, {"hecho", hecho}, {"resumen", resumen},
            {"resumenDelModelo", resumenDelModelo}};
}

TareaAgente TareaAgente::desdeJson(const nlohmann::json& j) {
    TareaAgente t;
    if (!j.is_object()) return t;
    t.pedido = j.value("pedido", std::string());
    t.objetivo = j.value("objetivo", std::string());
    for (const auto& h : j.value("hecho", nlohmann::json::array())) {
        if (h.is_string()) t.hecho.push_back(h.get<std::string>());
    }
    t.resumen = j.value("resumen", std::string());
    t.resumenDelModelo = j.value("resumenDelModelo", false);
    return t;
}

std::string bloqueTareaAnterior(const TareaAgente& anterior) {
    if (anterior.vacia()) return {};
    std::string s = "## Tarea anterior\nLo que hice:\n";
    if (anterior.hecho.empty()) s += "- (ninguna acción)\n";
    for (const auto& h : anterior.hecho) s += "- " + h + "\n";
    s += "Resumen: " + (anterior.resumen.empty() ? anterior.pedido : anterior.resumen) + "\n";
    return s;
}

std::string pedidoDeObjetivo(const TareaAgente& anterior, const std::string& pedido,
                             const std::string& documentacion) {
    std::string s = documentacion.empty() ? std::string() : documentacion + "\n";
    s += bloqueTareaAnterior(anterior);
    if (!s.empty()) s += "\n";
    return s + kEncabezadoTareaActual + "\n" + pedido +
           "\n\nEscribe SOLO una línea: \"Ahora: <lo que debes hacer en la tarea actual>\".";
}

std::string leerObjetivo(const std::string& respuesta) {
    static const std::regex ahora(R"((?:^|\n)[\s*"`]*Ahora\s*:\s*([^\n]+))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(respuesta, m, ahora)) return recortar(limpiarLinea(m[1].str()), kMaxResumen);
    return {};
}

std::string pedidoDeCierre(const std::string& pedido, const std::vector<llm::AgentAnswer::ExecutedTool>& acciones,
                           const std::string& respuesta) {
    std::string hechos;
    for (const auto& a : acciones) {
        hechos += std::string(a.ok ? "OK " : "FALLÓ ") + a.name + " " + recortar(a.argsJson, 200) + " → " +
                  recortar(a.result, 160) + "\n";
    }
    return "Terminaste una tarea. Tarea del usuario: " + pedido + "\nHerramientas que ejecutaste:\n" +
           (hechos.empty() ? "(ninguna)\n" : hechos) + "Tu respuesta final: " + recortar(respuesta, kMaxRespuesta) +
           "\n\nLo marcado FALLÓ no se hizo: no digas que se hizo. Resume SOLO lo que realmente hicieron las herramientas; "
           "no inventes acciones ni digas que borraste/listaste todo si solo hiciste parte.\n"
           "Escribe SOLO esto, en español:\nLo que hice:\n- <cada acción con su resultado: ids, rutas>\n"
           "Resumen: <una frase: qué había que hacer y cómo quedó realmente>";
}

bool leerCierre(const std::string& texto, TareaAgente& tarea) {
    static const std::regex resumen(R"((?:^|\n)[\s*]*Resumen\s*:\s*\**\s*([^\n]+))", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(texto, m, resumen)) return false;
    const std::string antes = texto.substr(0, static_cast<std::size_t>(m.position(0)));
    std::vector<std::string> hecho;
    std::size_t ini = 0;
    while (ini < antes.size() && hecho.size() < kMaxLineas) {
        auto fin = antes.find('\n', ini);
        if (fin == std::string::npos) fin = antes.size();
        std::string linea = sinEspacios(antes.substr(ini, fin - ini));
        if (!linea.empty() && (linea[0] == '-' || linea[0] == '*')) {
            linea = limpiarLinea(linea.substr(1));
            if (!linea.empty()) hecho.push_back(recortar(linea, kMaxLinea));
        }
        ini = fin + 1;
    }
    const std::string r = limpiarLinea(m[1].str());
    if (r.empty()) return false;
    tarea.hecho = std::move(hecho);
    tarea.resumen = recortar(r, kMaxResumen);
    tarea.resumenDelModelo = true;
    return true;
}

void cierreDeRespaldo(const std::vector<llm::AgentAnswer::ExecutedTool>& acciones, const std::string& respuesta,
                      TareaAgente& tarea) {
    tarea.hecho.clear();
    for (const auto& a : acciones) {
        if (tarea.hecho.size() >= kMaxLineas) break;
        tarea.hecho.push_back(recortar(a.name + " " + a.argsJson + (a.ok ? " → " : " → falló: ") + a.result, kMaxLinea));
    }
    tarea.resumen = recortar("Pediste: " + tarea.pedido + ". Respondí: " + sinEspacios(respuesta), kMaxResumen);
    tarea.resumenDelModelo = false;
}

std::string consultar(llm::LlmGateway& gateway, const std::string& pedido, const std::string& proyecto) {
    std::string sistema =
        "Eres Silvy, el agente del motor de juegos FunshiEngine. Trabajas con comandos sobre el proyecto del "
        "usuario: escenas con objetos y sus componentes (RigidBody, Collider…), y scripts en src/ (.java o .cpp). "
        "\"Objeto\" es un objeto de la escena.";
    if (!proyecto.empty()) sistema += " Proyecto de trabajo: \"" + proyecto + "\".";
    sistema += " Responde en español, breve.";
    const std::vector<llm::ChatMessage> mensajes = {llm::ChatMessage::system(sistema), llm::ChatMessage::user(pedido)};
    std::string texto;
    try {
        gateway.chatStream(mensajes, nullptr, [&](const llm::LlmChunk& c) {
            if (c.type == llm::LlmChunk::Type::Text) texto += c.delta;
        });
    } catch (const std::exception&) {
        return {};
    }
    // Razonamiento en línea (<think>…</think>) de algunos modelos: no es la respuesta.
    static const std::regex pensamiento(R"(<think>[\s\S]*?</think>)");
    return sinEspacios(std::regex_replace(texto, pensamiento, ""));
}

}  // namespace funshi::agente::tareas
