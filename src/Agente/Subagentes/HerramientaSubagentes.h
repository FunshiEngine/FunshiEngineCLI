/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Herramienta `lanzar_subagentes`: el agente principal delega tareas
// independientes en subagentes y recibe sus informes.

#include "Agente/LLM/Bucle/ToolRegistry.h"
#include "Agente/Subagentes/GestorSubagentes.h"

#include <memory>

namespace funshi::agente {

class LanzarSubagentesTool : public llm::Tool {
public:
    explicit LanzarSubagentesTool(std::shared_ptr<GestorSubagentes> gestor) : gestor_(std::move(gestor)) {}

    llm::ToolSpec spec() const override;
    llm::ToolResult execute(const nlohmann::json& args, const llm::ToolContext& ctx) override;

    // Valida y convierte los argumentos; deja el motivo en `error` si no sirven.
    static bool leerTareas(const nlohmann::json& args, std::vector<TareaSubagente>& tareas, std::string& error);

    // Tandas por pedido: una, y otra con los resultados de la primera. Más
    // que eso suele ser el modelo re-delegando "para confirmar" sin avanzar.
    static constexpr int kMaxTandas = 2;

private:
    std::shared_ptr<GestorSubagentes> gestor_;
    int tandas_ = 0;
};

}  // namespace funshi::agente
