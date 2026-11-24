/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Tui/Chat/ModeloChat.h"

namespace funshi::chat {

std::string rolATexto(MensajeChat::Rol r) {
    using Rol = MensajeChat::Rol;
    switch (r) {
        case Rol::Usuario: return "usuario";
        case Rol::Silvy: return "silvy";
        case Rol::Pensamiento: return "pensamiento";
        case Rol::Herramienta: return "herramienta";
        case Rol::Subagente: return "subagente";
        case Rol::Tarea: return "tarea";
        case Rol::Codigo: return "codigo";
        case Rol::Error: return "error";
        case Rol::Estado: return "estado";
    }
    return "estado";
}

MensajeChat::Rol rolDesdeTexto(const std::string& t) {
    using Rol = MensajeChat::Rol;
    if (t == "usuario") return Rol::Usuario;
    if (t == "silvy") return Rol::Silvy;
    if (t == "pensamiento") return Rol::Pensamiento;
    if (t == "herramienta") return Rol::Herramienta;
    if (t == "subagente") return Rol::Subagente;
    if (t == "tarea") return Rol::Tarea;
    if (t == "codigo") return Rol::Codigo;
    if (t == "error") return Rol::Error;
    return Rol::Estado;
}

MensajeChat mensajeDesdeSesion(const agente::MensajeSesion& m) {
    MensajeChat mc;
    mc.rol = rolDesdeTexto(m.rol);
    mc.texto = m.texto;
    mc.detalle = m.detalle;
    mc.ref = m.ref;
    // El estado de una herramienta/subagente va en su primer símbolo.
    mc.ok = m.texto.rfind("✘", 0) != 0;
    return mc;
}

agente::MensajeSesion mensajeASesion(const MensajeChat& m) {
    return {rolATexto(m.rol), m.texto, m.detalle, m.ref};
}

std::shared_ptr<SubagenteTui> SesionTui::subagente(const std::string& id) const {
    for (const auto& s : subagentes) {
        if (s->datos.id == id) return s;
    }
    return nullptr;
}

void SesionTui::cargarDesdeDatos() {
    mensajes.clear();
    for (const auto& m : datos.mensajes) mensajes.push_back(mensajeDesdeSesion(m));
    subagentes.clear();
    for (const auto& sa : datos.subagentes) {
        auto s = std::make_shared<SubagenteTui>();
        s->datos = sa;
        s->datos.mensajes.clear();
        for (const auto& m : sa.mensajes) s->mensajes.push_back(mensajeDesdeSesion(m));
        // Uno que quedó "en curso" al cerrar la TUI ya no corre.
        if (s->datos.estado == "en curso") s->datos.estado = "interrumpido";
        s->numero = static_cast<int>(subagentes.size()) + 1;
        subagentes.push_back(std::move(s));
    }
}

void SesionTui::volcarEnDatos() {
    datos.mensajes.clear();
    for (const auto& m : mensajes) {
        if (!m.enCurso) datos.mensajes.push_back(mensajeASesion(m));
    }
    datos.subagentes.clear();
    for (const auto& s : subagentes) {
        agente::SubagenteSesion sa = s->datos;
        sa.mensajes.clear();
        for (const auto& m : s->mensajes) sa.mensajes.push_back(mensajeASesion(m));
        datos.subagentes.push_back(std::move(sa));
    }
}

}  // namespace funshi::chat
