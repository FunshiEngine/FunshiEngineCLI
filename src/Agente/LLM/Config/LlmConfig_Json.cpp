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

// LlmConfig: validación y (de)serialización JSON.
#include "Agente/LLM/Config/LlmConfig.h"
#include "Agente/LLM/Local/LlamaBinarios.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace funshi::agente::llm {

std::vector<std::string> LlmConfig::validate() const {
    std::vector<std::string> errors;
    if (mode == LlmMode::REMOTE) {
        if (remoteBaseUrl.empty()) {
            errors.push_back("Modo REMOTE: falta remoteBaseUrl (ej. http://localhost:11434/v1)");
        }
        if (apiKey.empty() && !isLocalhostUrl()) {
            errors.push_back("Modo REMOTE: falta apiKey para " + remoteBaseUrl + " (endpoints localhost no la necesitan)");
        }
        if (modelName.empty()) {
            errors.push_back("Modo REMOTE: falta modelName");
        }
    }
    if (temperature < 0.0 || temperature > 2.0) {
        errors.push_back("temperature debe estar entre 0.0 y 2.0");
    }
    if (maxTokens <= 0) {
        errors.push_back("maxTokens debe ser > 0");
    }
    if (contextSize < 128 || contextSize > 40960) {
        errors.push_back("contextSize fuera de rango [128, 40960]");
    }
    return errors;
}

nlohmann::json LlmConfig::toJson() const {
    nlohmann::json j;
    j["mode"] = (mode == LlmMode::LOCAL) ? "local" : "remote";
    j["remoteBaseUrl"] = remoteBaseUrl;
    j["apiKey"] = apiKey;
    j["modelName"] = modelName;
    j["temperature"] = temperature;
    j["maxTokens"] = maxTokens;
    j["chatTimeoutMs"] = chatTimeoutMs;
    j["thinking"] = thinking;
    j["power"] = power;
    j["hfRepo"] = hfRepo;
    j["hfFile"] = hfFile;
    j["hfToken"] = hfToken;
    j["projectsDataDir"] = projectsDataDir;
    j["localInferenceDir"] = localInferenceDir;
    j["modelsDir"] = modelsDir;
    j["hfCacheDir"] = hfCacheDir;
    j["llamaServerPath"] = llamaServerPath;
    j["llamaRepoUrl"] = llamaRepoUrl;
    j["llamaRepoVersion"] = llamaRepoVersion;
    j["llamaRepoDir"] = llamaRepoDir;
    j["gitBin"] = gitBin;
    j["buildFromSource"] = buildFromSource;
    j["contextSize"] = contextSize;
    j["gpuLayers"] = gpuLayers;
    j["toolTimeoutMs"] = toolTimeoutMs;
    j["subagentesParalelos"] = subagentesParalelos;
    j["documentosDir"] = documentosDir;
    j["embeddingsUrl"] = embeddingsUrl;
    j["embeddingsModelo"] = embeddingsModelo;
    
    // Manual conversion for vectors of structs
    nlohmann::json mc = nlohmann::json::array();
    for (const auto& p : modelCatalog) {
        mc.push_back({{"id", p.id}, {"label", p.label}, {"repo", p.repo}, {"file", p.file}, {"sizeGb", p.sizeGb}, {"description", p.description}});
    }
    j["modelCatalog"] = mc;
    
    nlohmann::json ap = nlohmann::json::array();
    for (const auto& p : apiPresets) {
        ap.push_back({{"id", p.id}, {"label", p.label}, {"baseUrl", p.baseUrl}, {"apiKey", p.apiKey}, {"model", p.model}});
    }
    j["apiPresets"] = ap;
    
    return j;
}

LlmConfig LlmConfig::fromJson(const nlohmann::json& j) {
    LlmConfig cfg;
    std::string modeStr = j.value("mode", "remote");
    cfg.mode = (modeStr == "local") ? LlmMode::LOCAL : LlmMode::REMOTE;
    cfg.remoteBaseUrl = j.value("remoteBaseUrl", "");
    cfg.apiKey = j.value("apiKey", "");
    cfg.modelName = j.value("modelName", "phi-4-mini");
    cfg.temperature = j.value("temperature", 0.7);
    cfg.maxTokens = j.value("maxTokens", 4096);
    cfg.chatTimeoutMs = j.value("chatTimeoutMs", 0L);
    cfg.thinking = j.value("thinking", "off");
    cfg.power = j.value("power", "medium");
    cfg.hfRepo = j.value("hfRepo", "unsloth/Phi-4-mini-instruct-GGUF");
    cfg.hfFile = j.value("hfFile", "Phi-4-mini-instruct-Q4_K_M.gguf");
    cfg.hfToken = j.value("hfToken", "");
    cfg.projectsDataDir = j.value("projectsDataDir", "");
    cfg.localInferenceDir = j.value("localInferenceDir", "");
    cfg.modelsDir = j.value("modelsDir", "");
    // Compatibilidad: valores antiguos/por defecto que no representan una ruta explícita.
    if (cfg.modelsDir == "models/texto" || cfg.modelsDir == "models") {
        cfg.modelsDir.clear();
    }
    cfg.hfCacheDir = j.value("hfCacheDir", "");
    cfg.llamaServerPath = j.value("llamaServerPath", "");
    cfg.llamaRepoUrl = j.value("llamaRepoUrl", "https://github.com/ggml-org/llama.cpp.git");
    cfg.llamaRepoVersion = j.value("llamaRepoVersion", "");
    cfg.llamaRepoDir = j.value("llamaRepoDir", "");
    cfg.gitBin = j.value("gitBin", "git");
    cfg.buildFromSource = j.value("buildFromSource", true);
    cfg.contextSize = j.value("contextSize", kContextoPorDefecto);
    cfg.gpuLayers = j.value("gpuLayers", -1);
    cfg.toolTimeoutMs = j.value("toolTimeoutMs", 30000L);
    cfg.subagentesParalelos = j.value("subagentesParalelos", 0);
    cfg.documentosDir = j.value("documentosDir", "");
    cfg.embeddingsUrl = j.value("embeddingsUrl", "");
    cfg.embeddingsModelo = j.value("embeddingsModelo", "");
    
    // Manual conversion for vectors of structs
    if (j.contains("modelCatalog") && j["modelCatalog"].is_array()) {
        for (const auto& item : j["modelCatalog"]) {
            ModelPreset p;
            p.id = item.value("id", "");
            p.label = item.value("label", "");
            p.repo = item.value("repo", "");
            p.file = item.value("file", "");
            p.sizeGb = item.value("sizeGb", 0.0);
            p.description = item.value("description", "");
            cfg.modelCatalog.push_back(std::move(p));
        }
    }
    if (j.contains("apiPresets") && j["apiPresets"].is_array()) {
        for (const auto& item : j["apiPresets"]) {
            ApiPreset p;
            p.id = item.value("id", "");
            p.label = item.value("label", "");
            p.baseUrl = item.value("baseUrl", "");
            p.apiKey = item.value("apiKey", "");
            p.model = item.value("model", "");
            cfg.apiPresets.push_back(std::move(p));
        }
    }
    return cfg;
}

}  // namespace funshi::agente::llm
