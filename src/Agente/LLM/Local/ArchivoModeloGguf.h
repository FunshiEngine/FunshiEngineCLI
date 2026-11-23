/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <filesystem>
#include <string>

namespace funshi::agente::llm {

// Resultado de inspeccionar la cabecera de un archivo GGUF sin cargarlo.
struct EstadoArchivoModelo {
    bool esGguf = false;    // cabecera GGUF legible
    bool completo = true;   // false si el archivo está truncado (descarga a medias)
    std::string detalle;    // motivo legible cuando no es válido
};

// Lee la cabecera GGUF (metadatos + tabla de tensores) y comprueba que el
// archivo llegue al menos hasta el inicio del último tensor. Detecta
// descargas cortadas sin leer los GB de pesos. Cachea por ruta+tamaño+mtime.
// Archivos que no son GGUF (.bin) se dan por completos.
EstadoArchivoModelo inspeccionarArchivoModelo(const std::filesystem::path& ruta);

}  // namespace funshi::agente::llm
