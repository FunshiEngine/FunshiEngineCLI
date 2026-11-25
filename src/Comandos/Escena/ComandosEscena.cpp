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
// Comandos de escena de solo lectura y guardado.
#include "Comandos/Escena/ComandosEscena.h"

#include <Configuracion/ProjectManager.h>
#include <Configuracion/ProjectPaths.h>
#include <Assets/AssetPath.h>

#include "Escena/ContextoEscena.h"
#include "Escena/SerializacionEscena.h"
#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <Objetos/Componentes/Escena/Transform.h>
#include <Objetos/Componentes/Render/Model.h>
#include <Objetos/Componentes/Scripting/Script.h>
#include <Behaviour/ScriptRuntime.h>
#include <Behaviour/Reflection/BehaviourReflection.h>
#include <Objetos/Componentes/Render/Color.h>
#include <Objetos/Componentes/Audio/AudioSource.h>
#include <Objetos/Componentes/Colliders/EsfereCollider.h>
#include <Objetos/Componentes/Colliders/CubeCollider.h>
#include <Objetos/Componentes/RigidBody/RigidBody.h>

#include <filesystem>
#include <fstream>

#include "Comandos/Escena/ComandosEscenaInterno.h"
namespace funshi {

ResultadoComando escenaListar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
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

ResultadoComando escenaObjetos(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);
    nlohmann::json hierarchy = sceneHierarchyJson(&contexto.registro());
    return ResultadoComando::exito(
        {{"project", nombreProyecto}, {"root", hierarchy}});
}

ResultadoComando escenaObjetoObtener(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
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

ResultadoComando escenaObjetoPosicionGlobal(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
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

    Transform* tGlobal = obj->getGlobalTransform();
    if (!tGlobal) {
        registrarErrorOperacional("no_transform", "El objeto no tiene transform");
        return ResultadoComando::fallo("no_transform", "El objeto no tiene transform",
                                       codigosSalida::kRecursoNoEncontrado);
    }

    float* pos = tGlobal->getTranslatef();
    float* rot = tGlobal->getRotatef();
    float* sca = tGlobal->getScalef();

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", obj->getId()},
        {"name", obj->inputName},
        {"global", {
            {"position", {pos[0], pos[1], pos[2]}},
            {"rotation", {rot[0], rot[1], rot[2], rot[3]}},
            {"scale", {sca[0], sca[1], sca[2]}}
        }}
    });
}

ResultadoComando escenaGuardar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);
    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"saved", true}
    });
}

ResultadoComando escenaJerarquia(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);
    nlohmann::json hierarchy = sceneHierarchyJson(&contexto.registro());
    return ResultadoComando::exito(
        {{"project", nombreProyecto}, {"root", hierarchy}});
}
// ---------------------------------------------------------------------------
// scene.object.getglobaltransform
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoGetGlobalTransform(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"id", true, nlohmann::json::value_t::number_unsigned},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    int id = static_cast<int>(argumentos["id"].get<unsigned>());

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* obj = findObjectById(registro, id);
    if (!obj) {
        registrarErrorUso("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    Transform* globalTransform = obj->getGlobalTransform();
    if (!globalTransform) {
        registrarErrorOperacional("no_transform", "El objeto no tiene transform global");
        return ResultadoComando::fallo("no_transform", "El objeto no tiene transform global",
                                      codigosSalida::kFalloGeneral);
    }

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"position", {
            globalTransform->arrTranslatef[0],
            globalTransform->arrTranslatef[1],
            globalTransform->arrTranslatef[2]
        }},
        {"rotation", {
            globalTransform->arrRotatef[0],
            globalTransform->arrRotatef[1],
            globalTransform->arrRotatef[2],
            globalTransform->arrRotatef[3]
        }},
        {"scale", {
            globalTransform->arrScalef[0],
            globalTransform->arrScalef[1],
            globalTransform->arrScalef[2]
        }}
    });
}

}  // namespace funshi
