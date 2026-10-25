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

#include "Agente/LLM/HfApi.h"
#include "Agente/LLM/HfTypes.h"

#include <curl/curl.h>
#include <openssl/sha.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

// Helper C++17 para ends_with
static bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

namespace funshi::agente::llm {

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append(static_cast<char*>(contents), total);
    return total;
}

int ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* progressFn = static_cast<std::function<void(double)>*>(clientp);
    if (!progressFn) return 0;
    if (dltotal > 0 && dlnow >= 0) {
        double p = static_cast<double>(dlnow) / static_cast<double>(dltotal);
        if (p >= 0.0 && p <= 1.0) {
            (*progressFn)(p);
        }
    } else if (dlnow > 0) {
        // Indeterminate progress: server didn't provide Content-Length.
        // Report negative value to signal "downloading but unknown progress".
        (*progressFn)(-1.0);
    }
    return 0;
}

size_t WriteFileCallback(void* contents, size_t size, size_t nmemb, FILE* fp) {
    return fwrite(contents, size, nmemb, fp);
}

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

struct HfApi::Impl {
    CURL* curl = nullptr;
    std::string baseUrl = "https://huggingface.co/api";
    std::string userAgent = "FunshiEngineCLI/1.0";
    std::string lastError;

    Impl() {
        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        }
    }

    ~Impl() {
        if (curl) curl_easy_cleanup(curl);
    }

    const std::string& getLastError() const { return lastError; }
    void clearLastError() { lastError.clear(); }

    bool get(const std::string& url, std::string& response,
             const std::string& token,
             std::function<void(double)> progress,
             long timeoutMs) {
        if (!curl) return false;

        response.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L); // 10s connect timeout
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, progress ? 0L : 1L);
        if (progress) {
            curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, ProgressCallback);
            curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &progress);
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, ("User-Agent: " + userAgent).c_str());
        if (!token.empty()) {
            headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // SSL settings for HF API
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        if (res != CURLE_OK) {
            lastError = "cURL error: " + std::string(curl_easy_strerror(res));
            return false;
        }
        if (httpCode != 200) {
            lastError = "HTTP " + std::to_string(httpCode) + ": " + response;
            return false;
        }
        return true;
    }

    bool downloadFile(const std::string& url, const std::string& destPath,
                      const std::string& token,
                      std::function<void(double)> progress,
                      long /*timeoutMs*/) {
        if (!curl) return false;

        FILE* fp = fopen(destPath.c_str(), "wb");
        if (!fp) {
            lastError = "No se pudo crear archivo: " + destPath;
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 30000L); // 30s connect timeout
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, progress ? 0L : 1L);
        if (progress) {
            curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, ProgressCallback);
            curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &progress);
        }

        // Para descargas grandes: SIN timeout total (0 = sin límite).
        // Solo fail si la transferencia se estanca completamente por 120s.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 0L); // Sin timeout total
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L); // 1 KB/s mínimo
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);   // 120s sin progreso

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: */*");
        headers = curl_slist_append(headers, ("User-Agent: " + userAgent).c_str());
        if (!token.empty()) {
            headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // SSL settings for HF downloads
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        fclose(fp);

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        if (res != CURLE_OK) {
            lastError = "cURL error: " + std::string(curl_easy_strerror(res));
            std::filesystem::remove(destPath);
            return false;
        }
        if (httpCode != 200) {
            lastError = "HTTP " + std::to_string(httpCode);
            std::filesystem::remove(destPath);
            return false;
        }
        return true;
    }
};

HfApi::HfApi(std::string cacheDir) : cacheDir_(std::move(cacheDir)) {
    if (cacheDir_.empty()) {
        cacheDir_ = std::filesystem::path(std::getenv("HOME")) / ".agentes" / "hf-cache";
    }
    std::filesystem::create_directories(cacheDir_);
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
    if (!std::filesystem::exists(cacheFile)) return false;

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

SearchResult HfApi::parseSearchResponse(const nlohmann::json& json) {
    SearchResult result;
    if (!json.is_array()) return result;

    for (const auto& item : json) {
        ModelInfo model;
        model.id = item.value("modelId", item.value("id", ""));
        
        size_t slash = model.id.find('/');
        if (slash != std::string::npos) {
            model.author = model.id.substr(0, slash);
            model.name = model.id.substr(slash + 1);
        } else {
            model.author = "";
            model.name = model.id;
        }

        model.description = item.value("cardData", nlohmann::json::object()).value("description", "");
        model.pipelineTag = item.value("pipeline_tag", "");
        model.tags = item.value("tags", std::vector<std::string>{});
        model.downloads = item.value("downloads", 0L);
        model.likes = item.value("likes", 0L);
        model.lastModified = item.value("lastModified", "");
        model.license = item.value("cardData", nlohmann::json::object()).value("license", "");
        model.gated = item.value("gated", false);
        model.repoUrl = "https://huggingface.co/" + model.id;

        if (item.contains("siblings") && item["siblings"].is_array()) {
            for (const auto& sib : item["siblings"]) {
                if (!sib.contains("rfilename")) continue;
                std::string fname = sib["rfilename"].get<std::string>();
                if (endsWith(fname, ".gguf") || endsWith(fname, ".safetensors") || 
                    endsWith(fname, ".bin") || endsWith(fname, ".pt") || endsWith(fname, ".pth")) {
                    ModelVariant var;
                    var.filename = fname;
                    var.sizeGb = sib.value("size", 0L) / (1024.0 * 1024.0 * 1024.0);
                    var.downloadUrl = "https://huggingface.co/" + model.id + "/resolve/main/" + fname;
                    var.sha256 = sib.value("lfs", nlohmann::json::object()).value("sha256", "");
                    if (fname.find("Q4_K_M") != std::string::npos) var.quantization = "Q4_K_M";
                    else if (fname.find("Q4_K_S") != std::string::npos) var.quantization = "Q4_K_S";
                    else if (fname.find("Q5_K_M") != std::string::npos) var.quantization = "Q5_K_M";
                    else if (fname.find("Q5_K_S") != std::string::npos) var.quantization = "Q5_K_S";
                    else if (fname.find("Q6_K") != std::string::npos) var.quantization = "Q6_K";
                    else if (fname.find("Q8_0") != std::string::npos) var.quantization = "Q8_0";
                    else if (fname.find("FP16") != std::string::npos) var.quantization = "FP16";
                    else if (fname.find("F16") != std::string::npos) var.quantization = "FP16";
                    else if (fname.find("F32") != std::string::npos) var.quantization = "FP32";
                    model.variants.push_back(std::move(var));
                }
            }
        }

        result.models.push_back(std::move(model));
    }

    result.total = static_cast<int>(result.models.size());
    result.pageSize = static_cast<int>(result.models.size());
    result.hasMore = result.models.size() >= static_cast<size_t>(result.pageSize);
    return result;
}

ModelInfo HfApi::parseModelInfo(const nlohmann::json& json) {
    ModelInfo model;
    model.id = json.value("modelId", json.value("id", ""));
    
    size_t slash = model.id.find('/');
    if (slash != std::string::npos) {
        model.author = model.id.substr(0, slash);
        model.name = model.id.substr(slash + 1);
    } else {
        model.author = "";
        model.name = model.id;
    }

    model.description = json.value("cardData", nlohmann::json::object()).value("description", "");
    model.pipelineTag = json.value("pipeline_tag", "");
    model.tags = json.value("tags", std::vector<std::string>{});
    model.downloads = json.value("downloads", 0L);
    model.likes = json.value("likes", 0L);
    model.lastModified = json.value("lastModified", "");
    model.license = json.value("cardData", nlohmann::json::object()).value("license", "");
    model.gated = json.value("gated", false);
    model.repoUrl = "https://huggingface.co/" + model.id;

    if (json.contains("siblings") && json["siblings"].is_array()) {
        for (const auto& sib : json["siblings"]) {
            if (!sib.contains("rfilename")) continue;
            std::string fname = sib["rfilename"].get<std::string>();
            if (endsWith(fname, ".gguf") || endsWith(fname, ".safetensors") || 
                endsWith(fname, ".bin") || endsWith(fname, ".pt") || endsWith(fname, ".pth")) {
                ModelVariant var;
                var.filename = fname;
                var.sizeGb = sib.value("size", 0L) / (1024.0 * 1024.0 * 1024.0);
                var.downloadUrl = "https://huggingface.co/" + model.id + "/resolve/main/" + fname;
                var.sha256 = sib.value("lfs", nlohmann::json::object()).value("sha256", "");
                if (fname.find("Q4_K_M") != std::string::npos) var.quantization = "Q4_K_M";
                else if (fname.find("Q4_K_S") != std::string::npos) var.quantization = "Q4_K_S";
                else if (fname.find("Q5_K_M") != std::string::npos) var.quantization = "Q5_K_M";
                else if (fname.find("Q5_K_S") != std::string::npos) var.quantization = "Q5_K_S";
                else if (fname.find("Q6_K") != std::string::npos) var.quantization = "Q6_K";
                else if (fname.find("Q8_0") != std::string::npos) var.quantization = "Q8_0";
                else if (fname.find("FP16") != std::string::npos) var.quantization = "FP16";
                else if (fname.find("F16") != std::string::npos) var.quantization = "FP16";
                else if (fname.find("F32") != std::string::npos) var.quantization = "FP32";
                model.variants.push_back(std::move(var));
            }
        }
    }

    return model;
}

std::vector<ModelVariant> HfApi::parseVariants(const nlohmann::json& json, const std::string& modelId) {
    std::vector<ModelVariant> variants;
    if (json.contains("siblings") && json["siblings"].is_array()) {
        for (const auto& sib : json["siblings"]) {
            if (!sib.contains("rfilename")) continue;
            std::string fname = sib["rfilename"].get<std::string>();
            if (endsWith(fname, ".gguf") || endsWith(fname, ".safetensors") || 
                endsWith(fname, ".bin") || endsWith(fname, ".pt") || endsWith(fname, ".pth")) {
                ModelVariant var;
                var.filename = fname;
                var.sizeGb = sib.value("size", 0L) / (1024.0 * 1024.0 * 1024.0);
                var.downloadUrl = "https://huggingface.co/" + modelId + "/resolve/main/" + fname;
                var.sha256 = sib.value("lfs", nlohmann::json::object()).value("sha256", "");
                if (fname.find("Q4_K_M") != std::string::npos) var.quantization = "Q4_K_M";
                else if (fname.find("Q4_K_S") != std::string::npos) var.quantization = "Q4_K_S";
                else if (fname.find("Q5_K_M") != std::string::npos) var.quantization = "Q5_K_M";
                else if (fname.find("Q5_K_S") != std::string::npos) var.quantization = "Q5_K_S";
                else if (fname.find("Q6_K") != std::string::npos) var.quantization = "Q6_K";
                else if (fname.find("Q8_0") != std::string::npos) var.quantization = "Q8_0";
                else if (fname.find("FP16") != std::string::npos) var.quantization = "FP16";
                else if (fname.find("F16") != std::string::npos) var.quantization = "FP16";
                else if (fname.find("F32") != std::string::npos) var.quantization = "FP32";
                variants.push_back(std::move(var));
            }
        }
    }
    return variants;
}

SearchResult HfApi::search(const SearchFilters& filters,
                           std::function<void(double)> progress) {
    std::string url = buildSearchUrl(filters);
    std::string key = cacheKey(url);

    nlohmann::json cached;
    if (loadFromCache(key, cached)) {
        HfCacheEntry entry;
        entry.result = cached.value("result", SearchResult{});
        auto secs = cached.value("timestamp", 0L);
        entry.timestamp = std::chrono::system_clock::time_point(
            std::chrono::seconds(secs));
        if (!entry.expired()) {
            return entry.result;
        }
    }

    pImpl_->clearLastError();
    std::string response;
    if (!pImpl_->get(url, response, hfToken_, progress, timeoutMs_)) {
        throw std::runtime_error("Error HTTP en búsqueda: " + pImpl_->getLastError());
    }

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

    if (!forceRefresh) {
        nlohmann::json cached;
        if (loadFromCache(key, cached)) {
            HfCacheEntry entry;
            entry.result = cached.value("result", SearchResult{});
            auto secs = cached.value("timestamp", 0L);
            entry.timestamp = std::chrono::system_clock::time_point(
                std::chrono::seconds(secs));
            if (!entry.expired() && !entry.result.models.empty()) {
                return entry.result.models[0];
            }
        }
    }

    pImpl_->clearLastError();
    std::string response;
    if (!pImpl_->get(url, response, hfToken_, progress, timeoutMs_)) {
        throw std::runtime_error("Error HTTP: " + pImpl_->getLastError());
    }

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

bool HfApi::downloadVariant(const ModelVariant& variant,
                            const std::string& destPath,
                            std::function<void(double)> progress) {
    std::filesystem::create_directories(std::filesystem::path(destPath).parent_path());
    bool ok = pImpl_->downloadFile(variant.downloadUrl, destPath, hfToken_, progress, timeoutMs_);
    if (!ok) return false;

    // Verificar SHA256 si está disponible
    if (!variant.sha256.empty()) {
        std::string computed = computeSha256(destPath);
        if (computed != variant.sha256) {
            std::filesystem::remove(destPath);
            return false;  // Hash mismatch
        }
    }
    return true;
}

void HfApi::cleanCache() {
    if (!std::filesystem::exists(cacheDir_)) return;
    auto now = std::chrono::system_clock::now();
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir_)) {
        if (entry.path().extension() == ".json") {
            try {
                std::ifstream ifs(entry.path());
                nlohmann::json j;
                ifs >> j;
                HfCacheEntry cached;
                from_json(j, cached);
                if (cached.expired()) {
                    std::filesystem::remove(entry.path());
                }
            } catch (...) {
                std::filesystem::remove(entry.path());
            }
        }
    }
}

std::string HfApi::computeSha256(const std::string& filePath) const {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return "";

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    constexpr size_t BUFSIZE = 8192;
    char buffer[BUFSIZE];
    while (file.read(buffer, BUFSIZE) || file.gcount() > 0) {
        SHA256_Update(&ctx, buffer, file.gcount());
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

}  // namespace funshi::agente::llm
