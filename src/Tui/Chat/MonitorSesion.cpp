/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// MonitorSesion: reparte los eventos de un pedido entre la conversación de la
// sesión y la transcripción de cada subagente.
#include "Tui/Chat/MonitorChat.h"

#include <nlohmann/json.hpp>

#include <cctype>

namespace funshi::chat {

namespace {

using Rol = MensajeChat::Rol;
using Tipo = agente::llm::AgentEvent::Type;

constexpr std::size_t kMaxInformeResumen = 160;


std::string primeraLinea(const std::string& s) {
    const auto ini = s.find_first_not_of(" \n\r\t");
    if (ini == std::string::npos) return {};
    return s.substr(ini, s.find('\n', ini) - ini);
}

// "completado" o "parcial", según la línea RESULTADO: del informe.
std::string estadoDelInforme(const std::string& informe) {
    std::string bajo = informe;
    for (char& c : bajo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto pos = bajo.find("resultado:");
    if (pos == std::string::npos) return "completado";
    const std::string linea = bajo.substr(pos, bajo.find('\n', pos) - pos);
    if (linea.find("parcial") != std::string::npos && linea.find("completado") == std::string::npos) return "parcial";
    return "completado";
}

}  // namespace

// ---------------------------------------------------------------------------
// MonitorSesion

MonitorSesion::MonitorSesion(SesionTui& sesion) : sesion_(sesion), principal_(&sesion.mensajes) {}

void MonitorSesion::aplicar(const agente::llm::AgentEvent& e) {
    if (e.origen.empty()) {
        principal_.aplicar(e);
        return;
    }
    auto sub = sesion_.subagente(e.origen);
    if (e.type == Tipo::SubagenteInicio) {
        if (!sub) {
            sub = std::make_shared<SubagenteTui>();
            sub->datos.id = e.origen;
            sub->numero = static_cast<int>(sesion_.subagentes.size()) + 1;
            sesion_.subagentes.push_back(sub);
        }
        const auto j = nlohmann::json::parse(e.argsJson, nullptr, false);
        const auto campo = [&](const char* k) {
            return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
        };
        sub->datos.titulo = campo("titulo").empty() ? e.name : campo("titulo");
        sub->datos.perfil = campo("perfil");
        sub->datos.instrucciones = campo("instrucciones");
        sub->datos.contexto = campo("contexto");
        sub->datos.estado = "en curso";
        sub->indice = j.is_object() ? j.value("indice", 0) : 0;
        sub->total = j.is_object() ? j.value("total", 0) : 0;
        sub->enCurso = true;

        std::string tarea = sub->datos.titulo + "\nperfil de herramientas: " + sub->datos.perfil;
        if (!campo("proyecto").empty()) tarea += " · proyecto activo: " + campo("proyecto");
        tarea += "\n\ninstrucciones:\n" + sub->datos.instrucciones;
        if (!sub->datos.contexto.empty()) tarea += "\n\ncontexto:\n" + sub->datos.contexto;
        MensajeChat m;
        m.rol = Rol::Tarea;
        m.texto = tarea;
        sub->mensajes.push_back(std::move(m));

        auto& monitor = subagentes_[e.origen] = MonitorTranscripcion(&sub->mensajes);
        monitor.esperar();

        MensajeChat resumen;
        resumen.rol = Rol::Subagente;
        resumen.ref = e.origen;
        resumen.enCurso = true;
        sesion_.mensajes.push_back(std::move(resumen));
        resumenes_[e.origen] = static_cast<long>(sesion_.mensajes.size()) - 1;
        actualizarResumen(*sub);
        return;
    }
    if (!sub) return;
    auto it = subagentes_.find(e.origen);
    if (it == subagentes_.end()) it = subagentes_.emplace(e.origen, MonitorTranscripcion(&sub->mensajes)).first;

    if (e.type == Tipo::SubagenteFin) {
        it->second.cerrar(e.result, false, true);
        sub->enCurso = false;
        sub->datos.estado = !e.ok ? "fallido" : estadoDelInforme(e.result);
        if (!e.ok && !sub->mensajes.empty()) sub->mensajes.back().ok = false;
        sub->ultimaAccion.clear();
        actualizarResumen(*sub);
        return;
    }
    it->second.aplicar(e);
    if (e.type == Tipo::ToolStart) {
        ++sub->acciones;
        sub->ultimaAccion = describirLlamada(e.name, e.argsJson);
        actualizarResumen(*sub);
    } else if (e.type == Tipo::Pensamiento || e.type == Tipo::Text) {
        if (sub->ultimaAccion.empty() || sub->ultimaAccion.rfind("pensando", 0) != 0) {
            sub->ultimaAccion = e.type == Tipo::Pensamiento ? "pensando…" : "escribiendo…";
            actualizarResumen(*sub);
        }
    }
}

void MonitorSesion::actualizarResumen(SubagenteTui& sub) {
    const auto it = resumenes_.find(sub.datos.id);
    if (it == resumenes_.end()) return;
    MensajeChat& m = sesion_.mensajes[it->second];
    const std::string pos = sub.total > 0 ? std::to_string(sub.indice) + "/" + std::to_string(sub.total) + " " : "";
    const std::string acciones = std::to_string(sub.acciones) + (sub.acciones == 1 ? " acción" : " acciones");
    if (sub.enCurso) {
        m.texto = "⟳ " + pos + sub.datos.titulo + " · perfil " + sub.datos.perfil + " · " + acciones;
        if (!sub.ultimaAccion.empty()) m.texto += "\n  ahora: " + sub.ultimaAccion;
        m.ok = true;
    } else {
        const bool ok = sub.datos.estado == "completado" || sub.datos.estado == "parcial";
        m.texto = std::string(sub.datos.estado == "completado" ? "✔ " : ok ? "◐ " : "✘ ") + pos + sub.datos.titulo + " · " + sub.datos.estado + " · " + acciones;
        const std::string informe = sub.mensajes.empty() ? std::string() : primeraLinea(sub.mensajes.back().texto);
        if (!informe.empty()) m.texto += "\n  " + recortarTexto(informe, kMaxInformeResumen);
        m.ok = ok;
    }
    m.texto += "\n  clic aquí o /sub " + std::to_string(sub.numero) + " para ver su trabajo";
    m.enCurso = sub.enCurso;
    m.tocar();
}

void MonitorSesion::cerrar(const std::string& textoFinal, bool error) {
    // Subagentes que no llegaron a terminar (cancelación, error).
    for (auto& s : sesion_.subagentes) {
        if (!s->enCurso) continue;
        if (auto it = subagentes_.find(s->datos.id); it != subagentes_.end()) {
            it->second.cerrar("(interrumpido)", true);
        }
        s->enCurso = false;
        s->datos.estado = "interrumpido";
        actualizarResumen(*s);
    }
    principal_.cerrar(textoFinal, error);
}

}  // namespace funshi::chat
