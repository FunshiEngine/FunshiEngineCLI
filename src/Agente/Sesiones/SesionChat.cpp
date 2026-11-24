/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Sesiones/SesionChat.h"

#include "Agente/LLM/Config/LlmConfig.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>

namespace funshi::agente {

namespace fs = std::filesystem;

namespace {

std::int64_t ahora() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

nlohmann::json mensajeLlmAJson(const llm::ChatMessage& m) { return m.toJson(); }

llm::ChatMessage mensajeLlmDesdeJson(const nlohmann::json& j) {
    llm::ChatMessage m;
    m.role = j.value("role", std::string("user"));
    m.content = j.value("content", std::string());
    if (j.contains("tool_calls")) m.toolCalls = j["tool_calls"];
    if (j.contains("tool_call_id") && j["tool_call_id"].is_string()) m.toolCallId = j["tool_call_id"].get<std::string>();
    if (j.contains("name") && j["name"].is_string()) m.toolName = j["name"].get<std::string>();
    return m;
}

nlohmann::json mensajesAJson(const std::vector<MensajeSesion>& mensajes) {
    nlohmann::json mens = nlohmann::json::array();
    for (const auto& m : mensajes) {
        nlohmann::json j{{"rol", m.rol}, {"texto", m.texto}};
        if (!m.detalle.empty()) j["detalle"] = m.detalle;
        if (!m.ref.empty()) j["ref"] = m.ref;
        mens.push_back(std::move(j));
    }
    return mens;
}

std::vector<MensajeSesion> mensajesDesdeJson(const nlohmann::json& arr) {
    std::vector<MensajeSesion> out;
    if (!arr.is_array()) return out;
    for (const auto& m : arr) {
        if (!m.is_object()) continue;
        out.push_back({m.value("rol", std::string("estado")), m.value("texto", std::string()),
                       m.value("detalle", std::string()), m.value("ref", std::string())});
    }
    return out;
}

}  // namespace

nlohmann::json SesionChat::aJson() const {
    const nlohmann::json mens = mensajesAJson(mensajes);
    nlohmann::json subs = nlohmann::json::array();
    for (const auto& sa : subagentes) {
        subs.push_back({{"id", sa.id},
                        {"titulo", sa.titulo},
                        {"perfil", sa.perfil},
                        {"instrucciones", sa.instrucciones},
                        {"contexto", sa.contexto},
                        {"estado", sa.estado},
                        {"mensajes", mensajesAJson(sa.mensajes)}});
    }
    nlohmann::json hist = nlohmann::json::array();
    for (const auto& m : agente.historial) hist.push_back(mensajeLlmAJson(m));
    return {{"version", 1},          {"id", id},
            {"titulo", titulo},      {"creada", creada},
            {"actualizada", actualizada}, {"proyectoActivo", agente.proyectoActivo},
            {"mensajes", mens},      {"subagentes", subs},
            {"historialLlm", hist}, {"tareaAnterior", agente.tareaAnterior.aJson()}};
}

SesionChat SesionChat::desdeJson(const nlohmann::json& j) {
    SesionChat s;
    s.id = j.value("id", std::string());
    s.titulo = j.value("titulo", std::string());
    s.creada = j.value("creada", std::int64_t{0});
    s.actualizada = j.value("actualizada", s.creada);
    s.agente.proyectoActivo = j.value("proyectoActivo", std::string());
    s.agente.tareaAnterior = agente::tareas::TareaAgente::desdeJson(j.value("tareaAnterior", nlohmann::json::object()));
    s.mensajes = mensajesDesdeJson(j.value("mensajes", nlohmann::json::array()));
    for (const auto& sa : j.value("subagentes", nlohmann::json::array())) {
        if (!sa.is_object()) continue;
        SubagenteSesion sub;
        sub.id = sa.value("id", std::string());
        sub.titulo = sa.value("titulo", std::string());
        sub.perfil = sa.value("perfil", std::string());
        sub.instrucciones = sa.value("instrucciones", std::string());
        sub.contexto = sa.value("contexto", std::string());
        sub.estado = sa.value("estado", std::string());
        sub.mensajes = mensajesDesdeJson(sa.value("mensajes", nlohmann::json::array()));
        if (!sub.id.empty()) s.subagentes.push_back(std::move(sub));
    }
    for (const auto& m : j.value("historialLlm", nlohmann::json::array())) {
        if (m.is_object()) s.agente.historial.push_back(mensajeLlmDesdeJson(m));
    }
    return s;
}

SesionChat SesionChat::nueva(const std::string& titulo) {
    static std::atomic<int> contador{0};
    std::random_device rd;
    char sufijo[8];
    std::snprintf(sufijo, sizeof(sufijo), "%04x", static_cast<unsigned>(rd() & 0xffff));
    SesionChat s;
    s.creada = s.actualizada = ahora();
    s.id = std::to_string(s.creada) + "-" + sufijo;
    s.titulo = titulo.empty() ? "Sesión " + std::to_string(++contador) : titulo;
    return s;
}

std::string SesionChat::tituloDesde(const std::string& pedido) {
    std::string t;
    for (char c : pedido) t += (c == '\n' || c == '\t') ? ' ' : c;
    const auto ini = t.find_first_not_of(' ');
    if (ini == std::string::npos) return "Sesión";
    t = t.substr(ini, t.find_last_not_of(' ') - ini + 1);
    constexpr std::size_t kMax = 40;
    if (t.size() <= kMax) return t;
    std::size_t corte = t.rfind(' ', kMax);
    if (corte == std::string::npos || corte < 15) corte = kMax;
    while (corte > 0 && (static_cast<unsigned char>(t[corte]) & 0xC0) == 0x80) --corte;  // no partir UTF-8
    return t.substr(0, corte) + "…";
}

AlmacenSesiones::AlmacenSesiones(fs::path dir) : dir_(std::move(dir)) {}

fs::path AlmacenSesiones::dirPorDefecto() {
    return llm::LlmConfig::configPath().parent_path() / "sesiones";
}

std::vector<SesionChat> AlmacenSesiones::cargarTodas() const {
    std::vector<SesionChat> sesiones;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (e.path().extension() != ".json") continue;
        std::ifstream in(e.path());
        const auto j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object()) continue;
        SesionChat s = SesionChat::desdeJson(j);
        if (!s.id.empty()) sesiones.push_back(std::move(s));
    }
    std::sort(sesiones.begin(), sesiones.end(),
              [](const SesionChat& a, const SesionChat& b) { return a.actualizada > b.actualizada; });
    return sesiones;
}

bool AlmacenSesiones::guardar(const SesionChat& sesion) const {
    if (sesion.id.empty()) return false;
    SesionChat copia = sesion;
    auto& h = copia.agente.historial;
    if (h.size() > kMaxHistorial) {
        const bool conSistema = !h.empty() && h.front().role == "system";
        std::vector<llm::ChatMessage> recortado;
        if (conSistema) recortado.push_back(h.front());
        recortado.insert(recortado.end(), h.end() - static_cast<long>(kMaxHistorial - 1), h.end());
        h = std::move(recortado);
    }
    auto& m = copia.mensajes;
    if (m.size() > kMaxMensajes) m.erase(m.begin(), m.end() - static_cast<long>(kMaxMensajes));
    auto& subs = copia.subagentes;
    if (subs.size() > kMaxSubagentes) subs.erase(subs.begin(), subs.end() - static_cast<long>(kMaxSubagentes));
    for (auto& sa : subs) {
        if (sa.mensajes.size() > kMaxMensajes) {
            sa.mensajes.erase(sa.mensajes.begin(), sa.mensajes.end() - static_cast<long>(kMaxMensajes));
        }
    }

    std::error_code ec;
    fs::create_directories(dir_, ec);
    const fs::path destino = dir_ / (sesion.id + ".json");
    const fs::path tmp = destino.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << copia.aJson().dump();
        if (!out) return false;
    }
    fs::rename(tmp, destino, ec);
    return !ec;
}

bool AlmacenSesiones::borrar(const std::string& id) const {
    std::error_code ec;
    return !id.empty() && fs::remove(dir_ / (id + ".json"), ec);
}

}  // namespace funshi::agente
