/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Interno de LlamaBinarios: los dos pasos de red/disco de la actualización
// (definidos en LlamaBinarios_Release.cpp). No incluir fuera de Agente/LLM.

#include "Agente/LLM/Local/LlamaBinarios.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace funshi::agente::llm::detalle_llama {

// Release publicada de llama.cpp con binario para la plataforma actual.
struct ReleaseLlama {
    std::string tag;    // bNNNN
    std::string asset;  // llama-bNNNN-bin-<plataforma>.<ext>
    std::string url;
    std::uint64_t tamano = 0;
};

// Resultado de un paso: si falla, `error` es el motivo legible que se
// muestra junto a la versión instalada (si la hay).
struct ResultadoPaso {
    bool ok = false;
    bool sinConexion = false;
    std::string error;
};

using InformarProgreso = std::function<void(double, const std::string&)>;

// Consulta GitHub y elige la build bNNNN más reciente (no borrador) que
// publique binario para `plataforma`.
ResultadoPaso buscarUltimaRelease(const PlataformaLlama& plataforma, ReleaseLlama& release);

// Descarga, extrae e instala `release` en `dirBin/<tag>/`, registra la
// instalación en installed.json y borra las versiones anteriores.
// `accion` es el texto del progreso ("Descargando llama-server bNNNN").
ResultadoPaso instalarRelease(const std::filesystem::path& dirBin, const ReleaseLlama& release,
                              const PlataformaLlama& plataforma, const std::string& accion,
                              const InformarProgreso& informar, std::filesystem::path& servidor);

// Registro de la instalación vigente dentro de dirBin.
std::filesystem::path archivoRegistro(const std::filesystem::path& dirBin);

}  // namespace funshi::agente::llm::detalle_llama
