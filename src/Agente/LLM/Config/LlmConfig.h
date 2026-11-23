/*
 * Copyright 2024 FunshiEngine
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace funshi::agente::llm {

enum class LlmMode { LOCAL, REMOTE };

struct ModelPreset {
    std::string id;
    std::string label;
    std::string repo;
    std::string file;
    double sizeGb = 0.0;
    std::string description;
};

struct ApiPreset {
    std::string id;
    std::string label;
    std::string baseUrl;
    std::string apiKey;
    std::string model;
};

/**
 * Configuración del gateway LLM.
 * Persistida en `.agentes/llm-config.json`.
 */
constexpr int kContextoPorDefecto = 32768;

struct LlmConfig {
    // Modo general
    LlmMode mode = LlmMode::REMOTE;

    // ---- REMOTE ----
    std::string remoteBaseUrl = "";
    std::string apiKey = "";
    std::string modelName = "phi-4-mini";

    // Parámetros de generación
    double temperature = 0.7;
    int maxTokens = 4096;
    long chatTimeoutMs = 0;

    // Opcionales avanzados
    std::string thinking = "off";
    std::string power = "medium";

    // ---- LOCAL ----
    std::string hfRepo = "unsloth/Phi-4-mini-instruct-GGUF";
    std::string hfFile = "Phi-4-mini-instruct-Q4_K_M.gguf";
    std::string hfToken = "";
    // Rutas configurables (vacío = valor por defecto). Persistidas en la config.
    std::string projectsDataDir = "";    // Raíz de datos/proyectos del motor (FunshiEngine-DATA)
    std::string localInferenceDir = "";  // Base de inferencia local (llama.cpp, logs, etc.)
    std::string modelsDir = "";          // Directorio de modelos GGUF descargados
    std::string hfCacheDir = "";         // Cache de metadatos/consultas de Hugging Face
    std::string llamaServerPath = "";    // Binario llama-server (vacío = binario oficial descargado automáticamente)
    std::string llamaRepoUrl = "https://github.com/ggml-org/llama.cpp.git";
    std::string llamaRepoVersion = "";
    std::string llamaRepoDir = "";       // Obsoleto: ya no se clona/compila llama.cpp (se conserva por compatibilidad)
    std::string gitBin = "git";
    bool buildFromSource = true;

    int contextSize = kContextoPorDefecto;  // tokens de contexto del modelo local
    int gpuLayers = -1;

    // Catálogos
    std::vector<ModelPreset> modelCatalog;
    std::vector<ApiPreset> apiPresets;

    // Tools
    long toolTimeoutMs = 30000;

    // Serialización
    nlohmann::json toJson() const;
    static LlmConfig fromJson(const nlohmann::json& j);

    // Persistencia
    // Orden: $FUNSHI_CONFIG_DIR · `.agentes/` en el cwd o un padre · global
    // por usuario (~/.agentes, o %APPDATA%\\FunshiEngine en Windows).
    static std::filesystem::path configPath();
    // Carpeta respecto de la cual se resuelven las rutas relativas.
    static std::filesystem::path directorioBaseConfig();
    static LlmConfig load();
    void save() const;
    static LlmConfig defaults();

    // Utilidades
    int effectiveMaxTokens() const;
    bool isLocalhostUrl() const;
    std::vector<std::string> validate() const;

    // Rutas efectivas de inferencia local/modelos/cache (resueltas con defaults).
    // Raíz de datos del motor (la carpeta que contiene Proyects/). Tolera que
    // projectsDataDir apunte a la propia Proyects/ o a una carpeta hermana de
    // ella (p. ej. "Projects"): en ambos casos devuelve la raíz real. Vacía
    // si projectsDataDir no está configurado.
    std::filesystem::path projectsDataDirEfectivo() const;
    std::filesystem::path localInferenceDirEfectivo() const;
    std::filesystem::path modelsDirEfectivo() const;
    std::filesystem::path hfCacheDirEfectivo() const;
    std::filesystem::path llamaRepoDirEfectivo() const;
    std::filesystem::path llamaServerPathEfectivo() const;

    // Aplica a variables de entorno las rutas configuradas que deben verse al
    // resolver rutas del motor/proceso (p. ej. datos/proyectos). Seguro llamar
    // varias veces; no sobreescribe valores ya establecidos fuera si no hay
    // configuración explícita.
    void aplicarRutasEntorno() const;
};

} // namespace funshi::agente::llm