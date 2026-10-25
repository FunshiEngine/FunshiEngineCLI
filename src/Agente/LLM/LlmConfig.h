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
    std::string modelsDir = "models/texto";
    std::string llamaServerPath = "";
    std::string llamaRepoUrl = "https://github.com/ggml-org/llama.cpp.git";
    std::string llamaRepoVersion = "";
    std::string llamaRepoDir = "";
    std::string gitBin = "git";
    bool buildFromSource = true;

    int contextSize = 8192;
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
    static std::filesystem::path configPath();
    static LlmConfig load();
    void save() const;
    static LlmConfig defaults();

    // Utilidades
    int effectiveMaxTokens() const;
    bool isLocalhostUrl() const;
    std::vector<std::string> validate() const;
};

} // namespace funshi::agente::llm