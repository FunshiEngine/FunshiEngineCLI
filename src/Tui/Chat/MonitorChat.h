/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Monitor del chat: convierte los eventos del agente en mensajes, en el orden
// en que ocurren (razonamiento → herramienta → resultado → … → respuesta).
//
//   MonitorTranscripcion  una conversación (la de la sesión o la de un
//                         subagente).
//   MonitorSesion         reparte los eventos de un pedido: los del agente
//                         principal a la sesión, los de cada subagente a su
//                         propia transcripción, y mantiene en la sesión una
//                         línea de resumen por subagente.
//
// Sin hilos ni bloqueos propios: quien llama sostiene el mutex del chat.

#include "Agente/LLM/Config/LlmTypes.h"
#include "Tui/Chat/ModeloChat.h"

#include <map>
#include <string>
#include <vector>

namespace funshi::chat {

class MonitorTranscripcion {
public:
    explicit MonitorTranscripcion(std::vector<MensajeChat>* mensajes = nullptr) : mensajes_(mensajes) {}

    // Respuesta en curso vacía ("..."), para que se vea que espera al modelo.
    void esperar();
    // Text, Pensamiento, Estado, ToolStart y ToolEnd.
    void aplicar(const agente::llm::AgentEvent& e);
    // Fin del pedido. `textoFinal` se usa si hubo error o si no llegó nada en
    // vivo; con `siempreFinal` (informe de un subagente) también cuando el
    // último texto en vivo fue antes de una herramienta. Las herramientas que
    // quedaron abiertas se marcan interrumpidas.
    void cerrar(const std::string& textoFinal, bool error, bool siempreFinal = false);
    bool huboEventos() const { return huboEventos_; }

private:
    MensajeChat& nuevo(MensajeChat::Rol rol, std::string texto, bool enCurso);
    std::vector<MensajeChat>* mensajes_;
    long respuesta_ = -1;    // mensaje Silvy en curso
    long pensamiento_ = -1;  // razonamiento en curso
    std::map<std::string, long> herramientas_;  // callId → mensaje
    std::map<std::string, long> estados_;       // id de nota → mensaje
    std::map<std::string, std::string> argumentos_;
    bool huboEventos_ = false;
};

class MonitorSesion {
public:
    explicit MonitorSesion(SesionTui& sesion);

    void esperar() { principal_.esperar(); }
    void aplicar(const agente::llm::AgentEvent& e);
    void cerrar(const std::string& textoFinal, bool error);
    bool huboEventos() const { return principal_.huboEventos(); }

private:
    void actualizarResumen(SubagenteTui& sub);
    SesionTui& sesion_;
    MonitorTranscripcion principal_;
    std::map<std::string, MonitorTranscripcion> subagentes_;
    std::map<std::string, long> resumenes_;  // id del subagente → mensaje en la sesión
};

// Recorta a `max` bytes sin partir caracteres UTF-8 (agrega "…").
std::string recortarTexto(std::string s, std::size_t max);
std::string sinEspaciosExtremos(const std::string& s);

// Formato (FormatoChat.cpp).
// Texto corto de una llamada: "scene.object.create {"project":"X"}" para
// ejecutar_comando, "buscar_comandos «mover objeto»", etc.
std::string describirLlamada(const std::string& herramienta, const std::string& argsJson);
// Bloque con el contenido que una llamada escribe en un archivo (argumento
// "content", también dentro de ejecutar_comando): cabecera con la ruta y las
// líneas con "│ ". Vacío si no hay contenido. `completo` recibe todas las
// líneas cuando el bloque se recorta.
std::string bloqueDeCodigo(const std::string& herramienta, const std::string& argsJson,
                           std::string* completo = nullptr);
// Quita del texto los bloques ```tool_call (y una llamada JSON suelta):
// lo que queda es lo que el modelo "pensó" antes de llamar.
std::string quitarBloquesLlamada(const std::string& texto);

}  // namespace funshi::chat
