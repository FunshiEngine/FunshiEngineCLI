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

// HfApi: descarga de variantes, verificación SHA256 y limpieza de caché.
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
bool HfApi::downloadVariant(const ModelVariant& variant,
                            const std::string& destPath,
                            std::function<void(double, std::uint64_t, std::uint64_t)> progress) {
    pImpl_->clearLastError();
    lastOffline_ = false;
    http::OpcionesDescarga opciones;
    opciones.userAgent = userAgent_;
    opciones.tamanoEsperado = variant.sizeBytes;
    if (!hfToken_.empty()) opciones.cabeceras.push_back("Authorization: Bearer " + hfToken_);
    if (!variant.sha256.empty()) {
        const std::string esperado = variant.sha256;
        opciones.verificar = [this, esperado, progress](const std::filesystem::path& archivo,
                                                        std::string& error) {
            if (progress) progress(-1.0, 0, 0);
            if (computeSha256(archivo.string()) == esperado) return true;
            error = "El archivo descargado no coincide con el hash SHA256 publicado";
            return false;
        };
    }
    const auto r = http::descargar(
        variant.downloadUrl, destPath, opciones,
        [&progress](std::uint64_t recibidos, std::uint64_t total) {
            if (!progress) return;
            progress(total > 0 ? static_cast<double>(recibidos) / static_cast<double>(total) : -1.0,
                     recibidos, total);
        });
    if (!r.ok) {
        pImpl_->lastError = r.error;
        lastOffline_ = r.sinConexion;
        return false;
    }
    return true;
}

void HfApi::cleanCache() {
    std::error_code ecDir;
    if (!std::filesystem::is_directory(cacheDir_, ecDir)) return;
    try {
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
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
    } catch (...) {
        // Cache inaccesible: no es un error fatal.
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
