/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    SPDX-License-Identifier: Apache-2.0
*/
#include "Comandos/ComandosConfig.h"

#include <Configuracion/ProjectPaths.h>

#include "Agente/LLM/Cliente/LlmGateway.h"
#include "Agente/LLM/Config/LlmConfig.h"
#include "Nucleo/Registro.h"

#include <filesystem>
#include <string>
#include <vector>

namespace funshi {

namespace {

bool esCadena(const nlohmann::json& args, const char* clave) {
    return !args.contains(clave) || args[clave].is_string();
}

nlohmann::json rutasEfectivas(const agente::llm::LlmConfig& cfg) {
    nlohmann::json r;
    r["projectsDataDir"] = ProjectPaths::directorioBase();
    r["projectsDir"] = ProjectPaths::directorioProyects();
    r["localInferenceDir"] = cfg.localInferenceDirEfectivo().string();
    r["modelsDir"] = cfg.modelsDirEfectivo().string();
    r["hfCacheDir"] = cfg.hfCacheDirEfectivo().string();
    r["llamaServerPath"] = cfg.llamaServerPathEfectivo().string();
    return r;
}

nlohmann::json rutasConfiguradas(const agente::llm::LlmConfig& cfg) {
    nlohmann::json r;
    r["projectsDataDir"] = cfg.projectsDataDir;
    r["localInferenceDir"] = cfg.localInferenceDir;
    r["modelsDir"] = cfg.modelsDir;
    r["hfCacheDir"] = cfg.hfCacheDir;
    r["llamaServerPath"] = cfg.llamaServerPath;
    r["llamaRepoDir"] = cfg.llamaRepoDir;
    r["gitBin"] = cfg.gitBin;
    return r;
}

std::string mascaraSecreto(const std::string& valor) {
    if (valor.empty()) return {};
    if (valor.size() <= 4) return std::string(valor.size(), '*');
    return std::string("****") + valor.substr(valor.size() - 4);
}

nlohmann::json llmPublico(const agente::llm::LlmConfig& cfg) {
    nlohmann::json presets = nlohmann::json::array();
    for (const auto& p : cfg.apiPresets) {
        presets.push_back({
            {"id", p.id},
            {"label", p.label},
            {"baseUrl", p.baseUrl},
            {"model", p.model},
            {"hasApiKey", !p.apiKey.empty()},
        });
    }
    return {
        {"mode", cfg.mode == agente::llm::LlmMode::LOCAL ? "local" : "remote"},
        {"provider", cfg.providerEfectivo()},
        {"remoteBaseUrl", cfg.remoteBaseUrl},
        {"modelName", cfg.modelName},
        {"thinking", cfg.thinking},
        {"power", cfg.power},
        {"hasApiKey", !cfg.apiKey.empty()},
        {"hasGoogleApiKey", !cfg.googleApiKey.empty()},
        {"hasNvidiaApiKey", !cfg.nvidiaApiKey.empty()},
        {"apiKeyMasked", mascaraSecreto(cfg.apiKeyEfectiva())},
        {"apiPresets", presets},
    };
}

}  // namespace

ResultadoComando cliConfigLeer(const nlohmann::json&) {
    auto cfg = agente::llm::LlmConfig::load();
    return ResultadoComando::exito({
        {"configPath", agente::llm::LlmConfig::configPath().string()},
        {"paths", rutasConfiguradas(cfg)},
        {"effectivePaths", rutasEfectivas(cfg)},
        {"settings", {{"contextSize", cfg.contextSize}}},
        {"llm", llmPublico(cfg)},
    });
}

ResultadoComando cliConfigEscribir(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    const char* clavesTexto[] = {
        "projectsDataDir", "localInferenceDir", "modelsDir", "hfCacheDir",
        "llamaServerPath", "llamaRepoDir", "gitBin",
        "provider", "remoteBaseUrl", "modelName", "apiKey", "googleApiKey", "nvidiaApiKey",
        "thinking", "power",
    };
    for (const char* clave : clavesTexto) {
        if (!esCadena(argumentos, clave)) {
            const std::string mensaje = std::string("args.") + clave + " debe ser un string";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje,
                                           codigosSalida::kErrorUso);
        }
    }

    auto cfg = agente::llm::LlmConfig::load();
    bool cambios = false;

    // contextSize: entero (también se acepta como texto numérico).
    if (argumentos.contains("contextSize")) {
        const auto& v = argumentos["contextSize"];
        long long valor = -1;
        if (v.is_number_integer()) {
            valor = v.get<long long>();
        } else if (v.is_string()) {
            try {
                std::size_t usados = 0;
                const std::string t = v.get<std::string>();
                valor = t.empty() ? agente::llm::kContextoPorDefecto : std::stoll(t, &usados);
                if (!t.empty() && usados != t.size()) valor = -1;
            } catch (...) {
                valor = -1;
            }
        }
        if (valor < 512 || valor > 1048576) {
            const std::string mensaje = "args.contextSize debe ser un entero entre 512 y 1048576";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
        }
        cfg.contextSize = static_cast<int>(valor);
        cambios = true;
    }

    auto aplicar = [&](const char* clave, std::string& destino) {
        if (argumentos.contains(clave)) {
            destino = argumentos[clave].get<std::string>();
            cambios = true;
        }
    };
    aplicar("projectsDataDir", cfg.projectsDataDir);
    aplicar("localInferenceDir", cfg.localInferenceDir);
    aplicar("modelsDir", cfg.modelsDir);
    aplicar("hfCacheDir", cfg.hfCacheDir);
    aplicar("llamaServerPath", cfg.llamaServerPath);
    aplicar("llamaRepoDir", cfg.llamaRepoDir);
    aplicar("gitBin", cfg.gitBin);

    if (argumentos.contains("provider")) {
        const std::string provider = argumentos["provider"].get<std::string>();
        const bool conocido = provider.empty() || provider == "local" || provider == "openai" ||
                              provider == "google" || provider == "nvidia" || provider == "custom";
        if (!conocido) {
            const std::string mensaje = "args.provider debe ser google, nvidia, openai, custom, local o vacío";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
        }
        cfg.provider = provider;
        cambios = true;
        if (provider == "google") {
            cfg.remoteBaseUrl = "https://generativelanguage.googleapis.com/v1beta/openai/";
            if (cfg.modelName.empty() || cfg.modelName == "phi-4-mini") cfg.modelName.clear();
        } else if (provider == "nvidia") {
            cfg.remoteBaseUrl = "https://integrate.api.nvidia.com/v1";
            if (cfg.modelName.empty() || cfg.modelName == "phi-4-mini") cfg.modelName.clear();
        } else if (provider == "local") {
            if (cfg.remoteBaseUrl.empty()) cfg.remoteBaseUrl = "http://localhost:11434/v1";
        }
    }
    aplicar("remoteBaseUrl", cfg.remoteBaseUrl);
    aplicar("modelName", cfg.modelName);
    aplicar("apiKey", cfg.apiKey);
    aplicar("googleApiKey", cfg.googleApiKey);
    aplicar("nvidiaApiKey", cfg.nvidiaApiKey);
    if (argumentos.contains("thinking")) {
        const std::string v = argumentos["thinking"].get<std::string>();
        static const std::vector<std::string> ok = {"off", "low", "medium", "high", "xhigh"};
        if (std::find(ok.begin(), ok.end(), v) == ok.end()) {
            const std::string mensaje = "args.thinking debe ser off, low, medium, high o xhigh";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
        }
        cfg.thinking = v;
        cambios = true;
    }
    if (argumentos.contains("power")) {
        const std::string v = argumentos["power"].get<std::string>();
        static const std::vector<std::string> ok = {"low", "medium", "high", "xhigh"};
        if (std::find(ok.begin(), ok.end(), v) == ok.end()) {
            const std::string mensaje = "args.power debe ser low, medium, high o xhigh";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
        }
        cfg.power = v;
        cambios = true;
    }
    if ((cfg.provider == "google" || cfg.provider == "nvidia" || cfg.provider == "openai" || cfg.provider == "custom") &&
        !cfg.remoteBaseUrl.empty()) {
        cfg.mode = agente::llm::LlmMode::REMOTE;
    }

    if (!cambios) {
        const std::string mensaje =
            "No se indicó ningún valor a modificar (projectsDataDir, localInferenceDir, modelsDir, hfCacheDir, llamaServerPath, contextSize, provider, remoteBaseUrl, modelName, apiKey, googleApiKey, nvidiaApiKey, thinking, power)";
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje,
                                       codigosSalida::kErrorUso);
    }

    try {
        cfg.save();
    } catch (const std::exception& e) {
        const std::string mensaje = std::string("No se pudo guardar la configuración: ") + e.what();
        registrarErrorOperacional("config_write_failed", mensaje);
        return ResultadoComando::fallo("config_write_failed", mensaje,
                                       codigosSalida::kFalloEscritura);
    }

    // Aplicar al entorno del proceso actual cuando corresponda. La ubicación de
    // proyectos/datos resuelta por el motor puede quedar fijada en esta sesión;
    // el cambio persistido se aplica en nuevas ejecuciones.
    cfg.aplicarRutasEntorno();

    return ResultadoComando::exito({
        {"configPath", agente::llm::LlmConfig::configPath().string()},
        {"paths", rutasConfiguradas(cfg)},
        {"effectivePaths", rutasEfectivas(cfg)},
        {"settings", {{"contextSize", cfg.contextSize}}},
        {"llm", llmPublico(cfg)},
        {"applied", true},
        {"note", "La configuración fue guardada. Los cambios de ubicación de proyectos/datos pueden requerir reiniciar la CLI para aplicarse por completo en esta sesión."},
    });
}

ResultadoComando cliModelosListar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    const char* clavesTexto[] = {"provider", "remoteBaseUrl", "apiKey", "googleApiKey", "nvidiaApiKey"};
    for (const char* clave : clavesTexto) {
        if (!esCadena(argumentos, clave)) {
            const std::string mensaje = std::string("args.") + clave + " debe ser un string";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
        }
    }

    auto cfg = agente::llm::LlmConfig::load();
    if (argumentos.contains("provider")) {
        const std::string provider = argumentos["provider"].get<std::string>();
        const bool conocido = provider.empty() || provider == "local" || provider == "openai" ||
                              provider == "google" || provider == "nvidia" || provider == "custom";
        if (!conocido) {
            const std::string mensaje = "args.provider debe ser google, nvidia, openai, custom, local o vacío";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
        }
        cfg.provider = provider;
        if (provider == "google") cfg.remoteBaseUrl = "https://generativelanguage.googleapis.com/v1beta/openai/";
        if (provider == "nvidia") cfg.remoteBaseUrl = "https://integrate.api.nvidia.com/v1";
    }
    if (argumentos.contains("remoteBaseUrl")) cfg.remoteBaseUrl = argumentos["remoteBaseUrl"].get<std::string>();
    if (argumentos.contains("apiKey")) cfg.apiKey = argumentos["apiKey"].get<std::string>();
    if (argumentos.contains("googleApiKey")) cfg.googleApiKey = argumentos["googleApiKey"].get<std::string>();
    if (argumentos.contains("nvidiaApiKey")) cfg.nvidiaApiKey = argumentos["nvidiaApiKey"].get<std::string>();

    if (cfg.remoteBaseUrl.empty()) {
        const std::string mensaje = "Falta remoteBaseUrl para listar modelos";
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
    }
    const std::string key = cfg.apiKeyEfectiva();
    if (key.empty() && !cfg.isLocalhostUrl()) {
        const std::string mensaje = "Falta API key para listar modelos de " + cfg.providerEfectivo();
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
    }

    try {
        agente::llm::OpenAiCompatibleGateway gw(cfg.remoteBaseUrl, key, cfg.modelName.empty() ? "model" : cfg.modelName,
                                               cfg.temperature, cfg.effectiveMaxTokens(), cfg.chatTimeoutMs,
                                               cfg.thinking, cfg.power);
        const auto modelos = gw.listModels();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& m : modelos) arr.push_back(m);
        return ResultadoComando::exito({
            {"provider", cfg.providerEfectivo()},
            {"remoteBaseUrl", cfg.remoteBaseUrl},
            {"count", static_cast<int>(modelos.size())},
            {"models", arr},
        });
    } catch (const std::exception& e) {
        const std::string mensaje = std::string("No se pudieron listar modelos: ") + e.what();
        registrarErrorOperacional("llm_models_error", mensaje);
        return ResultadoComando::fallo("llm_models_error", mensaje, codigosSalida::kFalloGeneral);
    }
}

}  // namespace funshi
