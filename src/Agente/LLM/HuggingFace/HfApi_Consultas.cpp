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

// HfApi: búsqueda y metadatos de modelos (con caché y modo sin conexión).
#include "Agente/LLM/HuggingFace/HfApi.h"
#include "Agente/LLM/HuggingFace/HfTypes.h"
#include "Agente/LLM/Cliente/HttpDescarga.h"

#include <curl/curl.h>
#include <openssl/sha.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <vector>

#include "Agente/LLM/HuggingFace/HfApiImpl.h"

namespace funshi::agente::llm {
SearchResult HfApi::search(const SearchFilters& filters,
                           std::function<void(double)> progress) {
    std::string url = buildSearchUrl(filters);
    std::string key = cacheKey(url);

    nlohmann::json cached;
    std::optional<SearchResult> cacheVencido;
    if (loadFromCache(key, cached)) {
        HfCacheEntry entry;
        entry.result = cached.value("result", SearchResult{});
        auto secs = cached.value("timestamp", 0L);
        entry.timestamp = std::chrono::system_clock::time_point(
            std::chrono::seconds(secs));
        if (!entry.expired()) {
            return entry.result;
        }
        cacheVencido = entry.result;
    }

    pImpl_->clearLastError();
    std::string response;
    if (!pImpl_->get(url, response, hfToken_, progress, timeoutMs_)) {
        lastOffline_ = pImpl_->offline;
        // Sin red: mejor resultados viejos que nada.
        if (pImpl_->offline && cacheVencido) return *cacheVencido;
        throw std::runtime_error(pImpl_->offline ? pImpl_->getLastError()
                                                 : "Error HTTP en búsqueda: " + pImpl_->getLastError());
    }
    lastOffline_ = false;

    try {
        nlohmann::json json = nlohmann::json::parse(response);
        SearchResult result = parseSearchResponse(json);

        HfCacheEntry entry;
        entry.key = key;
        entry.result = result;
        entry.timestamp = std::chrono::system_clock::now();
        saveToCache(key, nlohmann::json(entry));

        return result;
    } catch (const std::exception& e) {
        throw std::runtime_error("Error parseando respuesta de búsqueda: " + std::string(e.what()));
    }
}

std::optional<ModelInfo> HfApi::getModelInfo(const std::string& modelId,
                                             std::function<void(double)> progress,
                                             bool forceRefresh) {
    std::string url = buildModelUrl(modelId);
    std::string key = cacheKey(url);

    std::optional<ModelInfo> enCache;
    {
        nlohmann::json cached;
        if (loadFromCache(key, cached)) {
            HfCacheEntry entry;
            entry.result = cached.value("result", SearchResult{});
            auto secs = cached.value("timestamp", 0L);
            entry.timestamp = std::chrono::system_clock::time_point(
                std::chrono::seconds(secs));
            if (!entry.result.models.empty()) {
                if (!forceRefresh && !entry.expired()) return entry.result.models[0];
                enCache = entry.result.models[0];
            }
        }
    }

    pImpl_->clearLastError();
    std::string response;
    if (!pImpl_->get(url, response, hfToken_, progress, timeoutMs_)) {
        lastOffline_ = pImpl_->offline;
        // Sin red: usar la última info conocida del modelo (aunque esté vencida).
        if (pImpl_->offline && enCache) return enCache;
        throw std::runtime_error(pImpl_->offline ? pImpl_->getLastError()
                                                 : "Error HTTP: " + pImpl_->getLastError());
    }
    lastOffline_ = false;

    try {
        nlohmann::json json = nlohmann::json::parse(response);
        ModelInfo model = parseModelInfo(json);

        SearchResult sr;
        sr.models.push_back(model);
        HfCacheEntry entry;
        entry.key = key;
        entry.result = sr;
        entry.timestamp = std::chrono::system_clock::now();
        saveToCache(key, nlohmann::json(entry));

        return model;
    } catch (const std::exception& e) {
        throw std::runtime_error("Error parseando respuesta: " + std::string(e.what()));
    }
}

std::vector<ModelVariant> HfApi::getVariants(const std::string& modelId,
                                             std::function<void(double)> progress,
                                             bool forceRefresh) {
    auto model = getModelInfo(modelId, progress, forceRefresh);
    if (!model) {
        throw std::runtime_error("No se pudo obtener info del modelo");
    }
    if (model->variants.empty()) {
        throw std::runtime_error("El modelo no tiene variantes GGUF/safetensors disponibles");
    }
    return model->variants;
}

}  // namespace funshi::agente::llm
