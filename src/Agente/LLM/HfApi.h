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

#include "Agente/LLM/HfTypes.h"
#include <string>
#include <functional>
#include <memory>

namespace funshi::agente::llm {

// Cliente HTTP nativo para Hugging Face Hub API
// Sin dependencias de FunshiEngineAI - implementación C++ directa
class HfApi {
public:
    explicit HfApi(std::string cacheDir = "");
    ~HfApi();

    // Busca modelos en Hugging Face
    // callback(progreso) se llama con 0.0-1.0 durante requests largas
    SearchResult search(const SearchFilters& filters,
                        std::function<void(double)> progress = nullptr);

    // Obtiene info completa de un modelo (incluye variantes/archivos)
    // forceRefresh=true ignora cache y hace request fresco a la API
    std::optional<ModelInfo> getModelInfo(const std::string& modelId,
                                          std::function<void(double)> progress = nullptr,
                                          bool forceRefresh = false);

    // Obtiene solo las variantes (archivos) de un modelo
    // forceRefresh=true ignora cache y hace request fresco a la API
    std::vector<ModelVariant> getVariants(const std::string& modelId,
                                          std::function<void(double)> progress = nullptr,
                                          bool forceRefresh = false);

    // Descarga un archivo de variante a disco
    // callback(progreso) se llama con bytes descargados / total
    bool downloadVariant(const ModelVariant& variant,
                         const std::string& destPath,
                         std::function<void(double)> progress = nullptr);

    // Limpia cache expirado
    void cleanCache();

    // Configuración
    void setTimeoutMs(int ms) { timeoutMs_ = ms; }
    void setUserAgent(const std::string& ua) { userAgent_ = ua; }
    void setToken(const std::string& token) { hfToken_ = token; } // Para modelos gated

    // Obtiene el último error de la operación fallida
    std::string getLastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;

    std::string cacheDir_;
    int timeoutMs_ = 30000;
    std::string userAgent_ = "FunshiEngineCLI/1.0";
    std::string hfToken_;

    // Helpers internos
    std::string buildSearchUrl(const SearchFilters& filters) const;
    std::string buildModelUrl(const std::string& modelId) const;
    std::string cacheKey(const std::string& url) const;
    bool loadFromCache(const std::string& key, nlohmann::json& out);
    void saveToCache(const std::string& key, const nlohmann::json& data);
    SearchResult parseSearchResponse(const nlohmann::json& json);
    ModelInfo parseModelInfo(const nlohmann::json& json);
    std::vector<ModelVariant> parseVariants(const nlohmann::json& json, const std::string& modelId);
    std::string computeSha256(const std::string& filePath) const;
};

}  // namespace funshi::agente::llm
