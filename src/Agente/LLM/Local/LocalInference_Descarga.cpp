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

// LocalInferenceManager: descarga y verificación de modelos GGUF.
#include "Agente/LLM/Local/LocalInference.h"
#include "Agente/LLM/HuggingFace/HfApi.h"
#include "Agente/LLM/Cliente/HttpDescarga.h"
#include "Agente/LLM/Local/LlamaBinarios.h"
#include "Agente/LLM/Local/RegistroServidor.h"

#include <curl/curl.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

// Network/socket headers
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>

namespace funshi::agente::llm {

namespace {
std::string computeSha256File(const std::filesystem::path& path,
                              const ProgressCallback& progress = nullptr,
                              const std::string& label = {}) {
    std::error_code ec;
    const auto total = std::filesystem::file_size(path, ec);
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    constexpr size_t BUFSIZE = 1024 * 1024;
    std::vector<char> buffer(BUFSIZE);
    std::uintmax_t processed = 0;
    std::uintmax_t lastReport = 0;
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize count = file.gcount();
        if (count > 0) {
            SHA256_Update(&ctx, buffer.data(), static_cast<size_t>(count));
            processed += static_cast<std::uintmax_t>(count);
            if (progress && total > 0 &&
                (processed - lastReport >= 16ULL * BUFSIZE || processed == total)) {
                lastReport = processed;
                double p = std::min(0.99, static_cast<double>(processed) /
                                              static_cast<double>(total));
                progress(p, label.empty() ? "Verificando modelo local..." : label);
            }
        }
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

} // namespace
namespace {

// Serializa las descargas de modelos: dos hilos escribiendo el mismo
// archivo parcial lo corromperían.
std::mutex& mutexDescargaModelos() {
    static auto* m = new std::mutex;  // nunca se destruye (hilos en segundo plano)
    return *m;
}

std::uint64_t tamanoEsperado(const ModelVariant& v) {
    if (v.sizeBytes > 0) return v.sizeBytes;
    return 0;
}

}  // namespace

bool LocalInferenceManager::downloadModel(const ModelVariant& variant,
                                          ProgressCallback progress) {
    std::lock_guard<std::mutex> lockDescarga(mutexDescargaModelos());
    setState(LocalInferenceState::DOWNLOADING_MODEL, "Preparando modelo: " + variant.filename);
    if (progress) progress(-1.0, "Buscando el modelo en disco: " + variant.filename);

    std::error_code ec;
    std::filesystem::create_directories(modelsDir_, ec);

    const std::filesystem::path destPath = modelsDir_ / variant.filename;
    const std::uint64_t esperado = tamanoEsperado(variant);

    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    auto equivalentPath = [](const std::filesystem::path& a, const std::filesystem::path& b) {
        std::error_code e;
        return std::filesystem::exists(a, e) && std::filesystem::exists(b, e) &&
               std::filesystem::equivalent(a, b, e);
    };

    std::vector<std::filesystem::path> candidates;
    auto addCandidate = [&](const std::filesystem::path& p) {
        std::error_code e;
        if (p.empty() || !std::filesystem::exists(p, e) || !std::filesystem::is_regular_file(p, e)) return;
        for (const auto& c : candidates) {
            if (equivalentPath(c, p)) return;
        }
        candidates.push_back(p);
    };

    const std::string wantedName = toLower(std::filesystem::path(variant.filename).filename().string());
    auto addDirCandidates = [&](const std::filesystem::path& dir) {
        std::error_code e;
        if (!std::filesystem::is_directory(dir, e)) return;
        addCandidate(dir / variant.filename);
        for (const auto& ent : std::filesystem::directory_iterator(dir, e)) {
            if (e) break;
            std::error_code e2;
            if (!ent.is_regular_file(e2)) continue;
            if (toLower(ent.path().filename().string()) == wantedName) {
                addCandidate(ent.path());
            }
        }
    };

    addCandidate(destPath);
    addDirCandidates(modelsDir_);
    addDirCandidates(baseDir_ / "models");

    // Compatibilidad con ubicaciones usadas previamente.
    const char* homeEnv = std::getenv("HOME");
#ifdef _WIN32
    if (!homeEnv) homeEnv = std::getenv("USERPROFILE");
#endif
    if (homeEnv) {
        addDirCandidates(std::filesystem::path(homeEnv) / ".agentes" / "local-inference" / "models");
    }
    addDirCandidates(std::filesystem::current_path(ec) / ".agentes" / "local-inference" / "models");

    // Un archivo completo es el que mide exactamente lo que publica Hugging
    // Face; si no se conoce el tamaño, se compara el hash.
    std::filesystem::path parcialMasGrande;
    std::uint64_t tamParcialMasGrande = 0;
    for (const auto& candidate : candidates) {
        std::error_code e;
        const std::uint64_t sz = std::filesystem::file_size(candidate, e);
        if (e || sz == 0) continue;

        bool reusable = false;
        if (esperado > 0) {
            reusable = (sz == esperado);
            if (!reusable && sz < esperado && sz > tamParcialMasGrande) {
                parcialMasGrande = candidate;
                tamParcialMasGrande = sz;
            }
        } else if (!variant.sha256.empty()) {
            const std::string hash = computeSha256File(
                candidate, progress, "Verificando modelo existente: " + candidate.filename().string());
            reusable = !hash.empty() && hash == variant.sha256;
        } else {
            reusable = sz > (100ULL * 1024 * 1024);
        }
        if (!reusable) continue;

        if (!equivalentPath(candidate, destPath)) {
            if (progress) progress(-1.0, "Reutilizando modelo existente: " + candidate.string());
            std::filesystem::create_directories(destPath.parent_path(), ec);
            // Copiar a un temporal y renombrar: el destino nunca queda a medias.
            const std::filesystem::path tmp = destPath.string() + ".tmp";
            std::filesystem::remove(tmp, ec);
            ec.clear();
            std::filesystem::create_hard_link(candidate, tmp, ec);
            if (ec) {
                ec.clear();
                std::filesystem::copy_file(candidate, tmp,
                                           std::filesystem::copy_options::overwrite_existing, ec);
            }
            if (!ec) std::filesystem::rename(tmp, destPath, ec);
            if (ec) {
                std::filesystem::remove(tmp, ec);
                setState(LocalInferenceState::ERROR,
                         "No se pudo reutilizar el modelo existente: " + ec.message());
                return false;
            }
        }

        setState(LocalInferenceState::IDLE, "Modelo ya descargado: " + variant.filename);
        if (progress) progress(1.0, "Modelo ya descargado, no se vuelve a bajar: " + variant.filename);
        return true;
    }

    // Una descarga incompleta (de una versión anterior que escribía directo
    // al nombre final) se convierte en el parcial para reanudar desde ahí.
    const std::filesystem::path parcial = http::rutaParcial(destPath);
    std::error_code ePar;
    const std::uint64_t tamParcialActual =
        std::filesystem::exists(parcial, ePar) ? std::filesystem::file_size(parcial, ePar) : 0;
    if (!parcialMasGrande.empty() && tamParcialMasGrande > tamParcialActual &&
        equivalentPath(parcialMasGrande, destPath)) {
        std::filesystem::rename(destPath, parcial, ec);
        ec.clear();
    }

    setState(LocalInferenceState::DOWNLOADING_MODEL, "Descargando modelo: " + variant.filename);
    if (progress) progress(0.0, "Descargando: " + variant.filename);

    const std::string nombre = variant.filename;
    auto adaptedProgress = [progress, nombre](double p, std::uint64_t recibidos, std::uint64_t total) {
        if (!progress) return;
        if (total > 0) {
            progress(p, "Descargando " + nombre + ": " + http::formatearBytes(recibidos) + " / " +
                            http::formatearBytes(total));
        } else {
            progress(-1.0, "Descargando " + nombre + ": " + http::formatearBytes(recibidos));
        }
    };

    HfApi api(modelsDir_.string());
    bool ok = api.downloadVariant(variant, destPath.string(), adaptedProgress);

    if (!ok) {
        setState(LocalInferenceState::ERROR, "Error descargando modelo: " + api.getLastError());
        return false;
    }

    setState(LocalInferenceState::IDLE, "Descarga completada: " + variant.filename);
    if (progress) progress(1.0, "Descarga completada: " + variant.filename);
    return true;
}

} // namespace funshi::agente::llm
