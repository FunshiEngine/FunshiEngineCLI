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
// Tag, estado activo y búsqueda de objetos.
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
// ---------------------------------------------------------------------------
// scene.object.settag
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoSetTag(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"id", true, nlohmann::json::value_t::number_unsigned},
        {"tag", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    int id = static_cast<int>(argumentos["id"].get<unsigned>());
    std::string tag = argumentos["tag"].get<std::string>();

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* obj = findObjectById(registro, id);
    if (!obj) {
        registrarErrorUso("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    std::string tagAnterior = obj->getTag();
    obj->setTag(tag);
    obj->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "settag", id,
                           nlohmann::json{{"tag", tagAnterior}},
                           nlohmann::json{{"tag", tag}});

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"tag", tag},
        {"tagged", true}
    });
}

// ---------------------------------------------------------------------------
// scene.object.setactive
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoSetActivo(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"id", true, nlohmann::json::value_t::number_unsigned},
        {"active", true, nlohmann::json::value_t::boolean},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    int id = static_cast<int>(argumentos["id"].get<unsigned>());
    bool activo = argumentos["active"].get<bool>();

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* obj = findObjectById(registro, id);
    if (!obj) {
        registrarErrorUso("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    bool activoAnterior = obj->getState();
    obj->setState(activo);
    obj->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "setactive", id,
                           nlohmann::json{{"active", activoAnterior}},
                           nlohmann::json{{"active", activo}});

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"active", activo},
        {"activated", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.findbytag
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoBuscarPorTag(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"tag", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    std::string tag = argumentos["tag"].get<std::string>();

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    nlohmann::json resultados = nlohmann::json::array();
    ListaDE<GameObject*>* lista = registro->getGameObjects();
    if (!lista->isEmpty()) {
        Position<GameObject*>* pos = lista->first();
        while (pos != nullptr) {
            GameObject* obj = pos->getElement();
            if (obj && obj->getTag() == tag) {
                resultados.push_back(nlohmann::json{
                    {"id", obj->getId()},
                    {"name", obj->inputName},
                    {"tag", obj->getTag()},
                    {"active", obj->getState()}
                });
            }
            pos = (pos != lista->last()) ? lista->next(pos) : nullptr;
        }
    }

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"tag", tag},
        {"objects", resultados},
        {"count", static_cast<int>(resultados.size())}
    });
}
// ---------------------------------------------------------------------------
// scene.object.findbyname
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoBuscarPorNombre(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"name", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    std::string nombre = argumentos["name"].get<std::string>();

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    nlohmann::json resultados = nlohmann::json::array();
    ListaDE<GameObject*>* lista = registro->getGameObjects();
    if (!lista->isEmpty()) {
        Position<GameObject*>* pos = lista->first();
        while (pos != nullptr) {
            GameObject* obj = pos->getElement();
            if (obj && obj->inputName == nombre) {
                resultados.push_back(nlohmann::json{
                    {"id", obj->getId()},
                    {"name", obj->inputName},
                    {"tag", obj->getTag()},
                    {"active", obj->getState()}
                });
            }
            pos = (pos != lista->last()) ? lista->next(pos) : nullptr;
        }
    }

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"name", nombre},
        {"objects", resultados},
        {"count", static_cast<int>(resultados.size())}
    });
}

}  // namespace funshi
