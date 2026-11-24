/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: el chat con el agente — razonamiento plegado y desplegado.
#include "PruebasAgente.h"

#include "Tui/AplicacionTui.h"

namespace pruebas_agente {

namespace {

bool contiene(const std::string& s, const std::string& parte) { return s.find(parte) != std::string::npos; }

}  // namespace

void probarChat() {
    // 1. Razonamiento plegado.
    {
        funshi::chat::MensajeChat m;
        m.rol = funshi::chat::MensajeChat::Rol::Pensamiento;
        m.texto = "uno\ndos\ntres\ncuatro\ncinco";
        std::vector<funshi::chat::LineaChat> lineas;
        funshi::AplicacionTui::partirMensajeChat(m, 60, lineas);
        CHECK(lineas.size() == 3 && contiene(lineas[2].texto, "3 líneas más · clic para ver el razonamiento"),
              "razonamiento: plegado en dos líneas y el aviso");
        m.expandido = true;
        funshi::AplicacionTui::partirMensajeChat(m, 60, lineas);
        CHECK(lineas.size() == 6 && contiene(lineas.back().texto, "clic para plegar"), "razonamiento: desplegado completo");
        m.texto = "corto";
        m.expandido = false;
        funshi::AplicacionTui::partirMensajeChat(m, 60, lineas);
        CHECK(lineas.size() == 1, "razonamiento: uno corto no se pliega");
    }

}

}  // namespace pruebas_agente
