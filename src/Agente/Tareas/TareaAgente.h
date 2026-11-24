/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Tarea del agente: cada pedido del usuario es una tarea. El modelo la abre
// escribiendo en una línea lo que debe hacer ("Ahora: …") con la tarea
// anterior a la vista, y la cierra escribiendo lo que hizo (una línea por
// acción) y un resumen. La tarea cerrada es el contexto de la siguiente.
//
// El formato es markdown en texto plano ("## Tarea anterior", "Lo que
// hice:", "Resumen:", "## Tarea actual", "Ahora:"): con modelos chicos es
// el que mejor se respeta y se entiende (frente a etiquetas tipo <lista>,
// renglones en mayúsculas o prosa).

#include "Agente/LLM/Config/LlmTypes.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace funshi::agente::llm {
class LlmGateway;
}

namespace funshi::agente::tareas {

struct TareaAgente {
    std::string pedido;               // lo que pidió el usuario
    std::string objetivo;             // lo que el modelo dijo que debía hacer
    std::vector<std::string> hecho;   // lo que hizo, una acción por línea
    std::string resumen;              // qué había que hacer y cómo quedó
    bool resumenDelModelo = false;    // false: cierre armado de las herramientas

    bool vacia() const { return pedido.empty(); }
    nlohmann::json aJson() const;
    static TareaAgente desdeJson(const nlohmann::json& j);
};

// "## Tarea anterior … ## Tarea actual" sin el pedido actual (lo agrega el
// bucle tras su encabezado). Vacío si no hay tarea anterior.
std::string bloqueTareaAnterior(const TareaAgente& anterior);
inline constexpr const char* kEncabezadoTareaActual = "## Tarea actual";

// Pedido para que el modelo escriba el objetivo, y su lectura ("Ahora: x" → x).
// `documentacion`: la del turno (comandos y documentos del usuario), para
// que el objetivo use los conceptos del motor.
std::string pedidoDeObjetivo(const TareaAgente& anterior, const std::string& pedido,
                             const std::string& documentacion = "");
std::string leerObjetivo(const std::string& respuesta);

// Pedido del cierre con lo que pasó en la tarea, y su lectura. leerCierre
// devuelve false si el modelo no respetó el formato.
std::string pedidoDeCierre(const std::string& pedido, const std::vector<llm::AgentAnswer::ExecutedTool>& acciones,
                           const std::string& respuesta);
bool leerCierre(const std::string& texto, TareaAgente& tarea);
// Cierre armado de las herramientas ejecutadas, para cuando el modelo falla.
void cierreDeRespaldo(const std::vector<llm::AgentAnswer::ExecutedTool>& acciones, const std::string& respuesta,
                      TareaAgente& tarea);

// Llamada corta sin herramientas; vacío si falla o se cancela. Lleva el
// contexto del dominio (motor de juegos, escenas, scripts y el proyecto de
// trabajo): sin él, "crea un objeto" se entiende como una clase.
std::string consultar(llm::LlmGateway& gateway, const std::string& pedido, const std::string& proyecto = "");

}  // namespace funshi::agente::tareas
