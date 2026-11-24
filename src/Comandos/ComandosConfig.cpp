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

#include "Agente/LLM/Config/LlmConfig.h"
#include "Nucleo/Registro.h"

#include <filesystem>
#include <string>

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

}  // namespace

ResultadoComando cliConfigLeer(const nlohmann::json&) {
    auto cfg = agente::llm::LlmConfig::load();
    return ResultadoComando::exito({
        {"configPath", agente::llm::LlmConfig::configPath().string()},
        {"paths", rutasConfiguradas(cfg)},
        {"effectivePaths", rutasEfectivas(cfg)},
        {"settings", {{"contextSize", cfg.contextSize}}},
    });
}

ResultadoComando cliConfigEscribir(const nlohmann::json& argumentos) {
    const char* claves[] = {
        "projectsDataDir", "localInferenceDir", "modelsDir", "hfCacheDir",
        "llamaServerPath", "llamaRepoDir", "gitBin",
    };
    for (const char* clave : claves) {
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

    if (!cambios) {
        const std::string mensaje =
            "No se indicó ningún valor a modificar (projectsDataDir, localInferenceDir, modelsDir, hfCacheDir, llamaServerPath, contextSize)";
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
        {"applied", true},
        {"note", "La configuración fue guardada. Los cambios de ubicación de proyectos/datos pueden requerir reiniciar la CLI para aplicarse por completo en esta sesión."},
    });
}

}  // namespace funshi
