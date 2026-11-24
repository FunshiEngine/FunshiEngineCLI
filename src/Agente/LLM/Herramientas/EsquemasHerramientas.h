/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Esquemas de las herramientas del motor generados de la ayuda de cada
// comando (Nucleo/AyudaComandos), la misma fuente que usa la base de
// conocimiento: así herramienta, documentación y comando no se contradicen.
// Cada argumento lleva tipo y descripción; "project" va primero.

#include "Agente/LLM/Bucle/ToolRegistry.h"

#include <memory>

namespace funshi::agente::llm {

// Esquema y descripción desde la ayuda del comando `original.name`. Del
// original solo conserva "resumen" (no es un argumento del comando). Sin
// ayuda para ese comando devuelve el original.
ToolSpec specDesdeAyuda(const ToolSpec& original);

// Envuelve una herramienta para que exponga specDesdeAyuda().
std::shared_ptr<Tool> conEsquemaDeAyuda(std::shared_ptr<Tool> herramienta);

}  // namespace funshi::agente::llm
