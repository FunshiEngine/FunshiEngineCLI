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

// LlmConfig: ubicación, carga/guardado y rutas efectivas. Validación y
// (de)serialización JSON en LlmConfig_Json.cpp.
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

namespace {

constexpr const char* kArchivoConfig = "llm-config.json";

// Carpeta de configuración por usuario cuando no hay una `.agentes/` en el
// directorio actual ni en sus padres.
std::filesystem::path directorioConfigGlobal() {
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA"); appData && *appData) {
        return std::filesystem::path(appData) / "FunshiEngine";
    }
    if (const char* perfil = std::getenv("USERPROFILE"); perfil && *perfil) {
        return std::filesystem::path(perfil) / ".agentes";
    }
#endif
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home && *home ? home : ".") / ".agentes";
}

// Cache de la última lectura: evita leer y parsear el JSON en cada llamada.
struct CacheConfig {
    std::filesystem::path ruta;
    std::filesystem::file_time_type modificado{};
    std::uintmax_t tamano = 0;
    LlmConfig config;
    bool valida = false;
};
std::mutex& mutexConfig() {
    static auto* m = new std::mutex;  // nunca se destruye: hay hilos en segundo plano
    return *m;
}
CacheConfig& cacheConfig() {
    static auto* c = new CacheConfig;
    return *c;
}

}  // namespace

std::filesystem::path LlmConfig::configPath() {
    // 1. Override explícito (tests, instalaciones portables).
    if (const char* dir = std::getenv("FUNSHI_CONFIG_DIR"); dir && *dir) {
        return std::filesystem::path(dir) / kArchivoConfig;
    }
    // 2. `.agentes/llm-config.json` en el directorio actual o en sus padres:
    //    funciona igual se abra la CLI desde la raíz del repo o desde build/.
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    if (!ec) {
        for (int nivel = 0; nivel < 32 && !dir.empty(); ++nivel) {
            const auto candidato = dir / ".agentes" / kArchivoConfig;
            if (std::filesystem::is_regular_file(candidato, ec)) return candidato;
            const auto padre = dir.parent_path();
            if (padre == dir) break;
            dir = padre;
        }
    }
    // 3. Configuración global por usuario.
    return directorioConfigGlobal() / kArchivoConfig;
}

std::filesystem::path LlmConfig::directorioBaseConfig() {
    // Las rutas relativas de la config se resuelven respecto de la carpeta que
    // contiene `.agentes/` (no del cwd), para que no cambien de significado.
    const auto dirConfig = configPath().parent_path();
    if (dirConfig.filename() == ".agentes") return dirConfig.parent_path();
    return dirConfig;
}

LlmConfig LlmConfig::load() {
    const auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return defaults();
    const auto modificado = std::filesystem::last_write_time(path, ec);
    const auto tamano = std::filesystem::file_size(path, ec);
    {
        std::lock_guard<std::mutex> lock(mutexConfig());
        const CacheConfig& c = cacheConfig();
        if (c.valida && c.ruta == path && c.modificado == modificado && c.tamano == tamano) {
            return c.config;
        }
    }
    try {
        std::ifstream f(path);
        nlohmann::json j = nlohmann::json::parse(f);
        LlmConfig cfg = fromJson(j);
        std::lock_guard<std::mutex> lock(mutexConfig());
        cacheConfig() = CacheConfig{path, modificado, tamano, cfg, true};
        return cfg;
    } catch (...) {
        // Si falla, config por defecto
    }
    return defaults();
}

void LlmConfig::save() const {
    const auto path = configPath();
    std::filesystem::create_directories(path.parent_path());
    // Escritura atómica: varios hilos (TUI, descargas) pueden guardar.
    auto temporal = path;
    temporal += ".tmp";
    {
        std::ofstream f(temporal, std::ios::trunc);
        if (!f) throw std::runtime_error("No se pudo escribir " + temporal.string());
        f << toJson().dump(2);
        if (!f) throw std::runtime_error("No se pudo escribir " + temporal.string());
    }
    std::error_code ec;
    std::filesystem::rename(temporal, path, ec);
    if (ec) {
        std::filesystem::remove(temporal, ec);
        throw std::runtime_error("No se pudo guardar " + path.string());
    }
    std::lock_guard<std::mutex> lock(mutexConfig());
    cacheConfig().valida = false;
}

LlmConfig LlmConfig::defaults() {
    LlmConfig cfg;
    cfg.modelCatalog = {
        {"phi-4-mini", "Phi-4-mini-instruct (Q4_K_M) · por defecto",
         "unsloth/Phi-4-mini-instruct-GGUF", "Phi-4-mini-instruct-Q4_K_M.gguf", 2.4,
         "Modelo por defecto: rápido y bueno en español para docs."},
        {"qwen3-4b", "Qwen3-4B-instruct (Q4_K_M)",
         "unsloth/Qwen3-4B-Instruct-2507-GGUF", "Qwen3-4B-Instruct-2507-Q4_K_M.gguf", 2.6,
         "Alternativa multilingüe compacta."},
        {"llama-3.2-3b", "Llama-3.2-3B-instruct (Q4_K_M)",
         "unsloth/Llama-3.2-3B-Instruct-GGUF", "Llama-3.2-3B-Instruct-Q4_K_M.gguf", 2.0,
         "El más liviano del catálogo."}
    };
    cfg.apiPresets = {};
    return cfg;
}

namespace {
std::filesystem::path resolverRutaConfig(const std::string& valor,
                                         const std::filesystem::path& defecto) {
    if (valor.empty()) return defecto;
    std::filesystem::path p(valor);
    if (p.is_absolute()) return p;
    return LlmConfig::directorioBaseConfig() / p;
}
}  // namespace

std::filesystem::path LlmConfig::localInferenceDirEfectivo() const {
    if (!localInferenceDir.empty()) {
        return resolverRutaConfig(localInferenceDir, {});
    }
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home) home = std::getenv("USERPROFILE");
#endif
    return std::filesystem::path(home ? home : ".") / ".agentes" / "local-inference";
}

std::filesystem::path LlmConfig::modelsDirEfectivo() const {
    if (!modelsDir.empty() && modelsDir != "models/texto") {
        return resolverRutaConfig(modelsDir, {});
    }
    return localInferenceDirEfectivo() / "models";
}

std::filesystem::path LlmConfig::hfCacheDirEfectivo() const {
    if (!hfCacheDir.empty()) {
        return resolverRutaConfig(hfCacheDir, {});
    }
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home) home = std::getenv("USERPROFILE");
#endif
    return std::filesystem::path(home ? home : ".") / ".agentes" / "hf-cache";
}

std::filesystem::path LlmConfig::llamaRepoDirEfectivo() const {
    if (!llamaRepoDir.empty()) {
        return resolverRutaConfig(llamaRepoDir, {});
    }
    return localInferenceDirEfectivo() / "llama.cpp";
}

std::filesystem::path LlmConfig::llamaServerPathEfectivo() const {
    if (!llamaServerPath.empty()) {
        return resolverRutaConfig(llamaServerPath, {});
    }
    // Binario oficial administrado (descargado en <localInferenceDir>/llama-bin).
    const std::filesystem::path instalado = rutaLlamaServerInstalado(localInferenceDirEfectivo());
    if (!instalado.empty()) return instalado;
    return localInferenceDirEfectivo() / "llama-bin";
}

void LlmConfig::aplicarRutasEntorno() const {
    if (!projectsDataDir.empty()) {
        std::filesystem::path p(projectsDataDir);
        if (!p.is_absolute()) p = directorioBaseConfig() / p;
        const std::string valor = p.string();
#ifdef _WIN32
        _putenv_s("FUNSHI_DATA_DIR", valor.c_str());
#else
        setenv("FUNSHI_DATA_DIR", valor.c_str(), 1);
#endif
    } else {
#ifdef _WIN32
        _putenv_s("FUNSHI_DATA_DIR", "");
#else
        unsetenv("FUNSHI_DATA_DIR");
#endif
    }
}

int LlmConfig::effectiveMaxTokens() const {
    if (power == "low") return 256;
    if (power == "high") return std::max(maxTokens, 4096);
    return maxTokens;
}

bool LlmConfig::isLocalhostUrl() const {
    if (remoteBaseUrl.empty()) return false;
    try {
        std::string host;
        size_t proto = remoteBaseUrl.find("://");
        size_t start = (proto == std::string::npos) ? 0 : proto + 3;
        size_t end = remoteBaseUrl.find(':', start);
        if (end == std::string::npos) end = remoteBaseUrl.find('/', start);
        if (end == std::string::npos) end = remoteBaseUrl.size();
        host = remoteBaseUrl.substr(start, end - start);
        std::transform(host.begin(), host.end(), host.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return host == "localhost" || host == "127.0.0.1" || host == "::1" ||
               (host.size() >= 10 && host.compare(host.size() - 10, 10, ".localhost") == 0);
    } catch (...) {
        return false;
    }
}

}  // namespace funshi::agente::llm
