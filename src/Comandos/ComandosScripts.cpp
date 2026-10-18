/*
    FunshiEngineCLI - CLI de automación del motor FunshiEngineGL
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
#include "Comandos/ComandosScripts.h"

#include <Assets/AssetPath.h>
#include <Behaviour/ScriptRuntime.h>
#include <Configuracion/ProjectManager.h>
#include <Configuracion/ProjectPaths.h>

#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <filesystem>

namespace funshi {

namespace {

// Resuelve args.project y el proyecto. Si falla, deja en `resultado` la
// respuesta de error y devuelve false.
bool resolverProyectoDesdeArgs(const nlohmann::json& argumentos,
                               std::string& nombreProyecto,
                               ResultadoComando& resultado) {
    std::string error;
    if (!argumentoProyecto(argumentos, nombreProyecto, error)) {
        registrarErrorUso("invalid_request", error);
        resultado = ResultadoComando::fallo("invalid_request", error,
                                            codigosSalida::kErrorUso);
        return false;
    }
    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        resultado = ResultadoComando::fallo("invalid_project", error,
                                            codigosSalida::kProyectoInvalido);
        return false;
    }
    return true;
}

}  // namespace

ResultadoComando scriptListar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::string scriptsDir = ProjectPaths::directorioScripts(nombreProyecto);
    std::error_code ec;
    nlohmann::json scripts = nlohmann::json::array();
    if (std::filesystem::exists(scriptsDir, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            if (ext == ".cpp" || ext == ".java") {
                scripts.push_back({
                    {"name", entry.path().filename().string()},
                    {"path", AssetPath::normalize(
                         std::filesystem::relative(entry.path(),
                                                   ProjectPaths::directorioProyecto(nombreProyecto))
                             .string())},
                    {"language", ext == ".cpp" ? "cpp" : "java"},
                    {"size", static_cast<long long>(entry.file_size(ec))}
                });
            }
        }
    }
    const long long cantidadScripts = static_cast<long long>(scripts.size());
    return ResultadoComando::exito({{"scripts", std::move(scripts)}, {"count", cantidadScripts}});
}

ResultadoComando scriptCompilar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::string scriptsDir = ProjectPaths::directorioScripts(nombreProyecto);
    std::error_code ec;
    nlohmann::json results = nlohmann::json::array();
    int compiled = 0, failed = 0, skipped = 0;
    if (std::filesystem::exists(scriptsDir, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            std::string sourcePath = entry.path().string();
            if (ext == ".cpp" || ext == ".java") {
                std::string className = AssetPath::basename(sourcePath);
                ComportamientoCargado comportamiento;
                std::string compileError;
                if (ScriptRuntime::compilarYCargar(sourcePath, className, comportamiento,
                                                   compileError)) {
                    results.push_back({{"file", entry.path().filename().string()},
                                       {"language", ext},
                                       {"status", "ok"},
                                       {"message", "Compilado y cargado correctamente"}});
                    compiled++;
                } else {
                    results.push_back({{"file", entry.path().filename().string()},
                                       {"language", ext},
                                       {"status", "error"},
                                       {"message", compileError}});
                    failed++;
                }
            }
        }
    }
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"compiled", compiled},
        {"failed", failed},
        {"skipped", skipped},
        {"results", std::move(results)}
    });
}

}  // namespace funshi
