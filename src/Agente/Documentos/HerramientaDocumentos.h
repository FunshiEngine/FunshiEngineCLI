/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// buscar_documentos: el agente consulta los documentos que el usuario dejó
// en su carpeta (PDF, Word, texto…). Solo lee; está en todos los perfiles.
// Usa el servidor de embeddings si ya está activo (no lo arranca) y, si no,
// la búsqueda léxica.

#include "Agente/LLM/Bucle/ToolRegistry.h"

namespace funshi::agente::documentos {

class BuscarDocumentosTool : public llm::Tool {
public:
    llm::ToolSpec spec() const override;
    llm::ToolResult execute(const nlohmann::json& args, const llm::ToolContext& ctx) override;
};

}  // namespace funshi::agente::documentos
