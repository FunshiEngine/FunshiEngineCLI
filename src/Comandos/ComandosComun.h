/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#ifndef FUNSHI_COMANDOS_COMUN_H
#define FUNSHI_COMANDOS_COMUN_H

// Utilidades compartidas por los manejadores de Comandos/*.cpp.

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

#include "Nucleo/Respuesta.h"

namespace funshi {

// Resuelve args.project y el proyecto. Si falla, deja en `resultado` la
// respuesta de error lista y devuelve false.
bool resolverProyectoDesdeArgs(const nlohmann::json& argumentos,
                               std::string& nombreProyecto,
                               ResultadoComando& resultado);

// Verifica que `destino` quede bajo `raiz` (rutas canónicas, comparación por
// componentes). Si escapa, deja el error en `resultado` y devuelve false.
bool rutaSegura(const std::filesystem::path& raiz, const std::filesystem::path& destino,
                const std::string& mensaje, ResultadoComando& resultado);

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_COMUN_H
