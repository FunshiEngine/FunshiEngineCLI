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

// HfApi: construcción, URLs y caché en disco. Consultas en HfApi_Consultas.cpp,
// parseo en HfApi_Parseo.cpp y descargas en HfApi_Descarga.cpp.
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

namespace {
std::string urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << std::setw(2)
                    << static_cast<int>(static_cast<unsigned char>(c)) << std::nouppercase;
        }
    }
    return escaped.str();
}

}  // namespace
HfApi::HfApi(std::string cacheDir) : cacheDir_(std::move(cacheDir)) {
    if (cacheDir_.empty()) {
        const char* home = std::getenv("HOME");
#ifdef _WIN32
        if (!home) home = std::getenv("USERPROFILE");
#endif
        cacheDir_ = (std::filesystem::path(home ? home : ".") / ".agentes" / "hf-cache").string();
    } else {
        std::filesystem::path p(cacheDir_);
        if (!p.is_absolute()) p = std::filesystem::absolute(p);
        cacheDir_ = p.string();
    }
    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);  // sin permisos: sin cache, no crash
    http::inicializarCurl();
    pImpl_ = std::make_unique<Impl>();
}

HfApi::~HfApi() = default;

std::string HfApi::getLastError() const {
    return pImpl_->getLastError();
}

std::string HfApi::buildSearchUrl(const SearchFilters& filters) const {
    std::ostringstream url;
    url << pImpl_->baseUrl << "/models?";

    if (!filters.query.empty()) {
        url << "search=" << urlEncode(filters.query) << "&";
    }
    if (filters.author) {
        url << "author=" << urlEncode(*filters.author) << "&";
    }
    if (filters.tag) {
        url << "filter=" << urlEncode(*filters.tag) << "&";
    }
    if (filters.pipeline) {
        url << "pipeline_tag=" << urlEncode(*filters.pipeline) << "&";
    }
    if (filters.library) {
        url << "library=" << urlEncode(*filters.library) << "&";
    }
    if (filters.license) {
        url << "license=" << urlEncode(*filters.license) << "&";
    }

    // Filtro por defecto gguf cuando no se especifica ningún filtro,
    // para que las búsquedas prioricen modelos GGUF (como FunshiEngineAI).
    if (!filters.author && !filters.tag && !filters.pipeline &&
        !filters.library && !filters.license) {
        url << "filter=gguf" << "&";
    }

    // Sin `expand` la API de búsqueda no devuelve `siblings` (los archivos)
    // y todos los modelos aparecían con 0 variantes. Al pedir `expand` solo
    // vuelven los campos listados, así que se piden todos los que se usan.
    url << "expand%5B%5D=siblings&expand%5B%5D=downloads&expand%5B%5D=likes&expand%5B%5D=tags"
           "&expand%5B%5D=pipeline_tag&expand%5B%5D=gated&expand%5B%5D=lastModified"
           "&expand%5B%5D=cardData&";

    url << "sort=" << urlEncode(filters.sort) << "&";
    url << "direction=" << urlEncode(filters.direction) << "&";
    url << "limit=" << filters.limit << "&";
    url << "page=" << filters.page << "&";

    return url.str();
}

std::string HfApi::buildModelUrl(const std::string& modelId) const {
    // Model ID path segments must NOT be fully URL-encoded (the '/' must remain as path separator)
    // Split by '/' and encode each segment individually
    std::ostringstream url;
    url << pImpl_->baseUrl << "/models/";
    std::string remaining = modelId;
    while (!remaining.empty()) {
        size_t slash = remaining.find('/');
        std::string segment = (slash == std::string::npos) ? remaining : remaining.substr(0, slash);
        url << urlEncode(segment);
        if (slash != std::string::npos) {
            url << "/";
            remaining = remaining.substr(slash + 1);
        } else {
            break;
        }
    }
    url << "?blobs=true";
    return url.str();
}

std::string HfApi::cacheKey(const std::string& url) const {
    std::hash<std::string> hasher;
    return std::to_string(hasher(url));
}

bool HfApi::loadFromCache(const std::string& key, nlohmann::json& out) {
    std::filesystem::path cacheFile = std::filesystem::path(cacheDir_) / (key + ".json");
    std::error_code ec;
    if (!std::filesystem::exists(cacheFile, ec)) return false;

    std::ifstream ifs(cacheFile);
    if (!ifs) return false;

    try {
        ifs >> out;
        return true;
    } catch (...) {
        return false;
    }
}

void HfApi::saveToCache(const std::string& key, const nlohmann::json& data) {
    std::filesystem::path cacheFile = std::filesystem::path(cacheDir_) / (key + ".json");
    std::ofstream ofs(cacheFile);
    if (ofs) {
        ofs << data.dump(2);
    }
}

}  // namespace funshi::agente::llm
