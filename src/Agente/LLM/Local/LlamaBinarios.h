/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Instalación de llama-server desde los binarios oficiales de llama.cpp
// (github.com/ggml-org/llama.cpp/releases), según el SO y la arquitectura.
// Sustituye al antiguo clonado + compilación local.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace funshi::agente::llm {

// Plataforma detectada en compilación.
struct PlataformaLlama {
    std::string so;            // "linux", "macos", "windows"
    std::string arquitectura;  // "x64", "arm64"
    std::string sufijoAsset;   // ej. "bin-ubuntu-x64.tar.gz"
    bool soportada = false;
};

PlataformaLlama detectarPlataformaLlama();

// Versión instalada (vacía si no hay ninguna válida).
struct InstalacionLlama {
    std::string tag;                     // ej. "b12644"
    std::string asset;
    std::filesystem::path servidor;      // ruta a llama-server
    bool valida() const { return !tag.empty() && !servidor.empty(); }
};

// Resultado de asegurar llama-server.
struct EstadoLlamaServer {
    bool ok = false;                 // hay un binario utilizable
    bool actualizado = false;        // se descargó una versión nueva
    bool sinConexion = false;        // no se pudo consultar/descargar por red
    std::string tag;                 // versión en uso
    std::filesystem::path servidor;  // binario a usar
    std::string mensaje;             // resumen legible (o error)
};

using ProgresoLlama = std::function<void(double progreso, const std::string& estado)>;

class InstaladorLlama {
public:
    // `baseDir`: directorio de inferencia local; los binarios van en
    // <baseDir>/llama-bin/<tag>/.
    explicit InstaladorLlama(std::filesystem::path baseDir);

    // Lee la instalación registrada (sin red).
    InstalacionLlama instalada() const;

    // Comprueba la última versión publicada y, si difiere de la instalada, la
    // descarga. Sin conexión usa la instalada si existe. Nunca lanza.
    EstadoLlamaServer asegurarActualizado(const ProgresoLlama& progreso = nullptr);

    std::filesystem::path directorioBinarios() const { return baseDir_ / "llama-bin"; }

private:
    std::filesystem::path baseDir_;
};

// Ruta efectiva del llama-server administrado (vacía si no hay instalación).
std::filesystem::path rutaLlamaServerInstalado(const std::filesystem::path& baseDir);

// Estado global de la última comprobación (para mostrarlo en la UI).
struct ResumenLlama {
    bool enCurso = false;
    bool ok = false;
    bool sinConexion = false;
    std::string mensaje;
};
ResumenLlama resumenLlama();

}  // namespace funshi::agente::llm
