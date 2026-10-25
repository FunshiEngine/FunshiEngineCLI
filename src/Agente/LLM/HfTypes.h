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
#include <chrono>
#include <nlohmann/json.hpp>

namespace funshi::agente::llm {

// Variante de un modelo (archivo GGUF, safetensors, etc.)
struct ModelVariant {
    std::string filename;
    std::string quantization;      // ej. "Q4_K_M", "Q8_0", "FP16"
    double sizeGb = 0.0;           // Tamaño en GB
    std::string downloadUrl;       // URL directa de descarga
    std::string sha256;            // Hash para verificación
};

// Información completa de un modelo en Hugging Face
struct ModelInfo {
    std::string id;                // ej. "TheBloke/Llama-2-7B-GGUF"
    std::string author;            // "TheBloke"
    std::string name;              // "Llama-2-7B-GGUF"
    std::string description;       // Descripción markdown
    std::string pipelineTag;       // "text-generation"
    std::vector<std::string> tags; // ["llama", "gguf", "quantized"]
    int64_t downloads = 0;
    int64_t likes = 0;
    std::string lastModified;      // ISO 8601
    std::vector<ModelVariant> variants;
    std::string license;           // ej. "llama-2", "mit", "apache-2.0"
    bool gated = false;            // Requiere aceptar condiciones
    std::string repoUrl;           // https://huggingface.co/...
};

// Filtros para búsqueda
struct SearchFilters {
    std::string query;             // Texto de búsqueda
    std::optional<std::string> author;   // Filtrar por autor
    std::optional<std::string> tag;      // Filtrar por tag (gguf, safetensors, etc.)
    std::optional<std::string> pipeline; // Filtrar por pipeline (text-generation)
    std::optional<std::string> library;  // Filtrar por library (llama.cpp, transformers)
    std::optional<std::string> license;  // Filtrar por licencia
    std::optional<double> minSizeGb;     // Tamaño mínimo en GB
    std::optional<double> maxSizeGb;     // Tamaño máximo en GB
    std::string sort = "downloads";      // downloads, likes, created, modified
    std::string direction = "-1";        // -1 (desc), 1 (asc)
    int limit = 20;
    int page = 0;
};

// Resultado de búsqueda paginado
struct SearchResult {
    std::vector<ModelInfo> models;
    int total = 0;
    int page = 1;
    int pageSize = 20;
    bool hasMore = false;
};

// Cache local para modelos HF
struct HfCacheEntry {
    std::string key;               // Hash de la query + filtros
    SearchResult result;
    std::chrono::system_clock::time_point timestamp;
    bool expired(std::chrono::hours ttl = std::chrono::hours(24)) const {
        return std::chrono::system_clock::now() - timestamp > ttl;
    }
};

// Serialización JSON para cache
inline void to_json(nlohmann::json& j, const ModelVariant& v) {
    j = nlohmann::json{
        {"filename", v.filename},
        {"quantization", v.quantization},
        {"sizeGb", v.sizeGb},
        {"downloadUrl", v.downloadUrl},
        {"sha256", v.sha256}
    };
}

inline void from_json(const nlohmann::json& j, ModelVariant& v) {
    v.filename = j.value("filename", "");
    v.quantization = j.value("quantization", "");
    v.sizeGb = j.value("sizeGb", 0.0);
    v.downloadUrl = j.value("downloadUrl", "");
    v.sha256 = j.value("sha256", "");
}

inline void to_json(nlohmann::json& j, const ModelInfo& m) {
    j = nlohmann::json{
        {"id", m.id},
        {"author", m.author},
        {"name", m.name},
        {"description", m.description},
        {"pipelineTag", m.pipelineTag},
        {"tags", m.tags},
        {"downloads", m.downloads},
        {"likes", m.likes},
        {"lastModified", m.lastModified},
        {"variants", m.variants},
        {"license", m.license},
        {"gated", m.gated},
        {"repoUrl", m.repoUrl}
    };
}

inline void from_json(const nlohmann::json& j, ModelInfo& m) {
    m.id = j.value("id", "");
    m.author = j.value("author", "");
    m.name = j.value("name", "");
    m.description = j.value("description", "");
    m.pipelineTag = j.value("pipelineTag", "");
    m.tags = j.value("tags", std::vector<std::string>{});
    m.downloads = j.value("downloads", 0L);
    m.likes = j.value("likes", 0L);
    m.lastModified = j.value("lastModified", "");
    m.variants = j.value("variants", std::vector<ModelVariant>{});
    m.license = j.value("license", "");
    m.gated = j.value("gated", false);
    m.repoUrl = j.value("repoUrl", "");
}

inline void to_json(nlohmann::json& j, const SearchResult& r) {
    j = nlohmann::json{
        {"models", r.models},
        {"total", r.total},
        {"page", r.page},
        {"pageSize", r.pageSize},
        {"hasMore", r.hasMore}
    };
}

inline void from_json(const nlohmann::json& j, SearchResult& r) {
    r.models = j.value("models", std::vector<ModelInfo>{});
    r.total = j.value("total", 0);
    r.page = j.value("page", 1);
    r.pageSize = j.value("pageSize", 20);
    r.hasMore = j.value("hasMore", false);
}

inline void to_json(nlohmann::json& j, const HfCacheEntry& e) {
    j = nlohmann::json{
        {"key", e.key},
        {"result", e.result},
        {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
            e.timestamp.time_since_epoch()).count()}
    };
}

inline void from_json(const nlohmann::json& j, HfCacheEntry& e) {
    e.key = j.value("key", "");
    e.result = j.value("result", SearchResult{});
    auto secs = j.value("timestamp", 0L);
    e.timestamp = std::chrono::system_clock::time_point(
        std::chrono::seconds(secs));
}

}  // namespace funshi::agente::llm
