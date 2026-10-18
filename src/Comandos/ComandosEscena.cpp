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
#include "Comandos/ComandosEscena.h"

#include <Configuracion/ProjectManager.h>
#include <Configuracion/ProjectPaths.h>

#include "Escena/ContextoEscena.h"
#include "Escena/SerializacionEscena.h"
#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <filesystem>

namespace funshi {

namespace {

// Resuelve args.project y el proyecto correspondiente. Si falla, deja en
// `resultado` la respuesta de error lista y devuelve false.
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

ResultadoComando escenaListar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::string sceneDir = ProjectPaths::rutaSceneDir(nombreProyecto);
    std::error_code ec;
    nlohmann::json scenes = nlohmann::json::array();
    if (std::filesystem::exists(sceneDir, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(sceneDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            if (ext == ".db" || ext == ".txt") {
                scenes.push_back({
                    {"name", entry.path().stem().string()},
                    {"file", entry.path().filename().string()},
                    {"size", static_cast<long long>(entry.file_size(ec))}
                });
            }
        }
    }
    const long long cantidadScenes = static_cast<long long>(scenes.size());
    return ResultadoComando::exito(
        {{"project", nombreProyecto}, {"scenes", std::move(scenes)}, {"count", cantidadScenes}});
}

ResultadoComando escenaObjetos(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);
    nlohmann::json hierarchy = sceneHierarchyJson(&contexto.registro());
    return ResultadoComando::exito(
        {{"project", nombreProyecto}, {"root", hierarchy}});
}

ResultadoComando escenaObjetoObtener(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);

    GameObject* obj = nullptr;
    if (argumentos.contains("id") && argumentos["id"].is_number_integer()) {
        obj = findObjectById(&contexto.registro(), argumentos["id"].get<int>());
    } else if (argumentos.contains("tag") && argumentos["tag"].is_string()) {
        obj = findObjectByTag(&contexto.registro(), argumentos["tag"].get<std::string>());
    } else {
        const std::string mensaje = "args.id (int) o args.tag (string) es requerido";
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
    }

    if (!obj) {
        registrarErrorOperacional("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                       codigosSalida::kRecursoNoEncontrado);
    }

    return ResultadoComando::exito(
        {{"project", nombreProyecto}, {"object", gameObjectJson(obj)}});
}

ResultadoComando escenaJerarquia(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);
    nlohmann::json hierarchy = sceneHierarchyJson(&contexto.registro());
    return ResultadoComando::exito(
        {{"project", nombreProyecto}, {"root", hierarchy}});
}

}  // namespace funshi
