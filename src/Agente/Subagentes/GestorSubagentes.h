/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Ejecuta tareas delegadas por el agente principal. Cada subagente es un
// AgentLoop nuevo, con su propio gateway (cURL no es seguro entre hilos),
// historial vacío, instrucciones de subagente y solo las herramientas de su
// perfil; nunca puede volver a delegar (profundidad 1).

#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/Subagentes/PlantillasAgente.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace funshi::agente {

struct BucleCreado {
    std::shared_ptr<llm::AgentLoop> bucle;
    std::set<std::string> herramientas;  // nombres, para el contexto del RAG
};

// Crea un bucle listo para un perfil de herramientas (sin lanzar_subagentes).
using FabricaBucles = std::function<BucleCreado(const std::string& perfil)>;

struct InformeSubagente {
    std::string id;            // único; es el `origen` de sus eventos
    std::string titulo;
    std::string perfil;
    bool ok = false;           // false si el informe dice "fallido" o hubo error
    std::string informe;       // respuesta final del subagente (recortada)
    int herramientasUsadas = 0;
};

class GestorSubagentes {
public:
    // `maxParalelo`: subagentes a la vez (1 = en orden).
    GestorSubagentes(FabricaBucles fabrica, int maxParalelo, std::string proyectoActivo = "");

    static constexpr std::size_t kMaxTareas = 4;
    static constexpr int kMaxRondas = 6;
    static constexpr std::size_t kMaxInforme = 1500;

    // Ejecuta las tareas y devuelve un informe por cada una, en el mismo
    // orden. `onEvent` recibe, serializado aunque corran en paralelo:
    // SubagenteInicio (con la tarea), todos los eventos del subagente con su
    // id en `origen`, y SubagenteFin (con el informe).
    std::vector<InformeSubagente> ejecutar(const std::vector<TareaSubagente>& tareas,
                                           const std::function<void(const llm::AgentEvent&)>& onEvent);

    // Texto que recibe el agente principal como resultado de la herramienta.
    static std::string resumen(const std::vector<InformeSubagente>& informes);

    static std::string nuevoId();

private:
    InformeSubagente ejecutarUna(const TareaSubagente& tarea, std::size_t indice, std::size_t total,
                                 const std::function<void(const llm::AgentEvent&)>& emitir);

    FabricaBucles fabrica_;
    int maxParalelo_;
    std::string proyectoActivo_;
};

}  // namespace funshi::agente
