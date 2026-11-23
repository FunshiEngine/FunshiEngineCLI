/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Tui/Chat/MonitorChat.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace funshi::chat {

namespace {

using Rol = MensajeChat::Rol;
using Tipo = agente::llm::AgentEvent::Type;

constexpr std::size_t kMaxDescripcion = 160;
constexpr std::size_t kMaxVistaPrevia = 240;
constexpr int kLineasVistaPrevia = 3;

std::string recortarTexto(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t corte = max;
    while (corte > 0 && (static_cast<unsigned char>(s[corte]) & 0xC0) == 0x80) --corte;  // no partir UTF-8
    return s.substr(0, corte) + "…";
}

std::string sinEspaciosExtremos(const std::string& s) {
    const auto ini = s.find_first_not_of(" \n\r\t");
    if (ini == std::string::npos) return {};
    return s.substr(ini, s.find_last_not_of(" \n\r\t") - ini + 1);
}

// JSON legible si el texto es JSON; si no, el texto tal cual.
std::string legible(const std::string& s) {
    const auto j = nlohmann::json::parse(s, nullptr, false);
    return j.is_discarded() ? s : j.dump(2);
}

// Primeras líneas del resultado. `recortado` indica si quedó algo afuera.
std::string vistaPrevia(const std::string& resultado, bool& recortado) {
    const auto j = nlohmann::json::parse(resultado, nullptr, false);
    std::string s = j.is_discarded() ? resultado : j.dump();
    std::string out;
    int lineas = 0;
    std::size_t pos = 0;
    while (pos < s.size() && lineas < kLineasVistaPrevia) {
        auto fin = s.find('\n', pos);
        if (fin == std::string::npos) fin = s.size();
        out += (lineas ? "\n" : "") + s.substr(pos, fin - pos);
        ++lineas;
        pos = fin + 1;
    }
    recortado = pos < s.size() || out.size() > kMaxVistaPrevia;
    return recortarTexto(out, kMaxVistaPrevia);
}
}  // namespace

std::string describirLlamada(const std::string& herramienta, const std::string& argsJson) {
    const auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
    std::string s;
    if (herramienta == "ejecutar_comando" && args.is_object() && args.contains("comando")) {
        s = args["comando"].is_string() ? args["comando"].get<std::string>() : args["comando"].dump();
        if (args.contains("argumentos")) s += " " + args["argumentos"].dump();
    } else if (herramienta == "buscar_comandos" && args.is_object() && args.contains("consulta")) {
        s = "buscar_comandos «" + (args["consulta"].is_string() ? args["consulta"].get<std::string>()
                                                                 : args["consulta"].dump()) + "»";
    } else if (herramienta == "lanzar_subagentes" && args.is_object() && args.contains("tareas") &&
               args["tareas"].is_array()) {
        s = "lanzar_subagentes · " + std::to_string(args["tareas"].size()) + " tarea(s)";
    } else {
        s = herramienta;
        if (args.is_object() && !args.empty()) s += " " + args.dump();
        else if (args.is_discarded() && !argsJson.empty()) s += " " + argsJson;
    }
    return recortarTexto(s, kMaxDescripcion);
}

std::string quitarBloquesLlamada(const std::string& texto) {
    std::string s = texto;
    for (const char* cerco : {"```tool_call", "```json"}) {
        std::size_t pos;
        while ((pos = s.find(cerco)) != std::string::npos) {
            const auto cierre = s.find("```", pos + 3);
            const std::string bloque = s.substr(pos, cierre == std::string::npos ? std::string::npos : cierre + 3 - pos);
            // ```json solo se quita si es una llamada; un JSON explicativo queda.
            if (std::string(cerco) == "```json" && bloque.find("\"tool") == std::string::npos) break;
            s.erase(pos, bloque.size());
        }
    }
    // Donde estaba el bloque quedan renglones vacíos: como mucho uno seguido.
    std::string compacto;
    for (char c : s) {
        if (c == '\n' && compacto.size() >= 2 && compacto[compacto.size() - 1] == '\n' &&
            compacto[compacto.size() - 2] == '\n') {
            continue;
        }
        compacto += c;
    }
    s = sinEspaciosExtremos(compacto);
    // Llamada escrita sin cerco: el texto entero es un objeto JSON.
    if (!s.empty() && s.front() == '{' && s.back() == '}' &&
        !nlohmann::json::parse(s, nullptr, false).is_discarded()) {
        return {};
    }
    return s;
}

// ---------------------------------------------------------------------------
// MonitorTranscripcion

MensajeChat& MonitorTranscripcion::nuevo(Rol rol, std::string texto, bool enCurso) {
    MensajeChat m;
    m.rol = rol;
    m.texto = std::move(texto);
    m.enCurso = enCurso;
    mensajes_->push_back(std::move(m));
    return mensajes_->back();
}

void MonitorTranscripcion::esperar() {
    nuevo(Rol::Silvy, "", true);
    respuesta_ = static_cast<long>(mensajes_->size()) - 1;
}

void MonitorTranscripcion::aplicar(const agente::llm::AgentEvent& e) {
    huboEventos_ = true;
    auto& msgs = *mensajes_;
    switch (e.type) {
        case Tipo::Text: {
            if (e.delta.empty()) return;
            if (pensamiento_ >= 0) {
                msgs[pensamiento_].enCurso = false;
                msgs[pensamiento_].tocar();
                pensamiento_ = -1;
            }
            if (respuesta_ < 0) {
                nuevo(Rol::Silvy, "", true);
                respuesta_ = static_cast<long>(msgs.size()) - 1;
            }
            msgs[respuesta_].texto += e.delta;
            msgs[respuesta_].tocar();
            return;
        }
        case Tipo::Pensamiento: {
            if (e.delta.empty()) return;
            if (pensamiento_ < 0) {
                // El razonamiento va antes de la respuesta: si la respuesta en
                // curso sigue vacía, se reutiliza ese lugar.
                if (respuesta_ >= 0 && msgs[respuesta_].texto.empty()) {
                    msgs[respuesta_].rol = Rol::Pensamiento;
                    pensamiento_ = respuesta_;
                    respuesta_ = -1;
                } else {
                    nuevo(Rol::Pensamiento, "", true);
                    pensamiento_ = static_cast<long>(msgs.size()) - 1;
                }
            }
            msgs[pensamiento_].texto += e.delta;
            msgs[pensamiento_].tocar();
            return;
        }
        case Tipo::Estado: {
            if (!e.callId.empty()) {
                if (auto it = estados_.find(e.callId); it != estados_.end()) {
                    msgs[it->second].texto = e.delta;
                    msgs[it->second].tocar();
                    return;
                }
            }
            // La nota va antes de la respuesta en curso si esta sigue vacía.
            long pos = static_cast<long>(msgs.size());
            if (respuesta_ >= 0 && msgs[respuesta_].texto.empty() && respuesta_ == pos - 1) {
                MensajeChat nota;
                nota.rol = Rol::Estado;
                nota.texto = e.delta;
                msgs.insert(msgs.begin() + respuesta_, std::move(nota));
                pos = respuesta_++;
            } else {
                nuevo(Rol::Estado, e.delta, false);
            }
            if (!e.callId.empty()) estados_[e.callId] = pos;
            return;
        }
        case Tipo::ToolStart: {
            // Lo que el modelo escribió antes de la llamada es su razonamiento.
            long lugar = -1;
            if (respuesta_ >= 0) {
                MensajeChat& r = msgs[respuesta_];
                const std::string pensado = quitarBloquesLlamada(r.texto);
                if (pensado.empty()) {
                    lugar = respuesta_;  // la respuesta vacía pasa a ser la herramienta
                } else {
                    r.rol = Rol::Pensamiento;
                    r.texto = pensado;
                    r.enCurso = false;
                    r.tocar();
                }
            }
            if (pensamiento_ >= 0) msgs[pensamiento_].enCurso = false;
            respuesta_ = pensamiento_ = -1;
            if (lugar < 0) {
                nuevo(Rol::Herramienta, "", true);
                lugar = static_cast<long>(msgs.size()) - 1;
            }
            MensajeChat& h = msgs[lugar];
            h.rol = Rol::Herramienta;
            h.texto = "⟳ " + describirLlamada(e.name, e.argsJson);
            h.detalle = "herramienta: " + e.name + "\nargumentos:\n" + legible(e.argsJson.empty() ? "{}" : e.argsJson);
            h.enCurso = true;
            h.ok = true;
            h.tocar();
            herramientas_[e.callId] = lugar;
            argumentos_[e.callId] = e.argsJson;
            return;
        }
        case Tipo::ToolEnd: {
            long lugar = -1;
            if (auto it = herramientas_.find(e.callId); it != herramientas_.end()) lugar = it->second;
            if (lugar < 0) {
                nuevo(Rol::Herramienta, "", false);
                lugar = static_cast<long>(msgs.size()) - 1;
            }
            MensajeChat& h = msgs[lugar];
            bool recortado = false;
            const std::string previa = vistaPrevia(e.result, recortado);
            h.texto = std::string(e.ok ? "✔ " : "✘ ") + describirLlamada(e.name, argumentos_[e.callId]);
            if (!previa.empty()) h.texto += "\n→ " + previa;
            if (recortado) h.texto += "\n  (clic para ver el resultado completo)";
            if (h.detalle.empty()) h.detalle = "herramienta: " + e.name;
            h.detalle += "\nresultado:\n" + legible(e.result);
            h.enCurso = false;
            h.ok = e.ok;
            h.tocar();
            herramientas_.erase(e.callId);
            return;
        }
        case Tipo::SubagenteInicio:
        case Tipo::SubagenteFin:
            return;  // los atiende MonitorSesion
    }
}

void MonitorTranscripcion::cerrar(const std::string& textoFinal, bool error, bool siempreFinal) {
    auto& msgs = *mensajes_;
    for (auto& [id, lugar] : herramientas_) {
        MensajeChat& h = msgs[lugar];
        if (h.texto.rfind("⟳ ", 0) == 0) h.texto = "✘ " + h.texto.substr(std::string("⟳ ").size()) + " (interrumpida)";
        h.enCurso = false;
        h.ok = false;
        h.tocar();
    }
    herramientas_.clear();
    if (pensamiento_ >= 0) msgs[pensamiento_].enCurso = false;

    // Una llamada que no llegó a ejecutarse (mal formada) se muestra tal
    // cual: es justo lo que hay que ver para entender qué falló.
    const auto visible = [](const std::string& t) {
        const std::string limpio = quitarBloquesLlamada(t);
        return limpio.empty() ? sinEspaciosExtremos(t) : limpio;
    };
    std::string texto = respuesta_ >= 0 ? visible(msgs[respuesta_].texto) : std::string();
    // Con eventos, el texto final del bucle repite lo ya mostrado arriba
    // (razonamientos, notas de corte): solo se usa si no hubo nada en vivo.
    if (error || (texto.empty() && (!huboEventos_ || siempreFinal))) texto = visible(textoFinal);
    if (texto.empty() && huboEventos_) texto = "(Silvy no dio una respuesta final; arriba están sus acciones)";
    if (texto.empty()) texto = "(sin respuesta)";
    if (respuesta_ < 0) {
        nuevo(Rol::Silvy, "", false);
        respuesta_ = static_cast<long>(msgs.size()) - 1;
    }
    MensajeChat& r = msgs[respuesta_];
    r.texto = texto;
    r.rol = error ? Rol::Error : Rol::Silvy;
    r.enCurso = false;
    r.tocar();
    respuesta_ = pensamiento_ = -1;
}

}  // namespace funshi::chat
