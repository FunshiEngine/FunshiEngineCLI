/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Herramientas de conocimiento y ejecución genérica:
//   buscar_comandos  — consulta la base de conocimiento (RAG) del agente;
//   ejecutar_comando — ejecuta cualquier comando del catálogo vía Harness,
//                      para los que no tienen herramienta propia.

#include "Agente/LLM/Herramientas/EngineTools.h"

#include <set>
#include <string>

namespace funshi::agente::llm {

class BuscarComandosTool : public Tool {
public:
    // `herramientas`: nombres de las tools del registro (para indicar en cada
    // comando si tiene herramienta propia).
    explicit BuscarComandosTool(std::set<std::string> herramientas)
        : herramientas_(std::move(herramientas)) {}
    ToolSpec spec() const override;
    ToolResult execute(const nlohmann::json& args, const ToolContext& ctx) override;

private:
    std::set<std::string> herramientas_;
};

class EjecutarComandoTool : public EngineTool {
public:
    using EngineTool::EngineTool;
    ToolSpec spec() const override;
    ToolResult execute(const nlohmann::json& args, const ToolContext& ctx) override;

    // Comandos que el agente no puede ejecutar por esta vía (destructivos o
    // que cambian la configuración de la CLI): el usuario los corre con /comando.
    static const std::set<std::string>& bloqueados();

    // Resultados más largos se recortan para no llenar el contexto.
    static constexpr std::size_t kMaxResultado = 6000;
};

}  // namespace funshi::agente::llm
