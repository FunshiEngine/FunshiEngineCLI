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
// Creación de objetos y cambios de jerarquía (padre/hijo).
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
// scene.object.setparent
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoSetPadre(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"id", true, nlohmann::json::value_t::number_unsigned},
        {"parentId", true, nlohmann::json::value_t::number_unsigned},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    unsigned idU = argumentos["id"].get<unsigned>(); int id = static_cast<int>(idU);
    unsigned parentIdU = argumentos["parentId"].get<unsigned>(); int parentId = static_cast<int>(parentIdU);

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* hijo = findObjectById(registro, id);
    if (!hijo) {
        registrarErrorOperacional("not_found", "Objeto hijo no encontrado: " + std::to_string(id));
        return ResultadoComando::fallo("not_found", "Objeto hijo no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    GameObject* padre = nullptr;
    if (parentId != 0) {
        padre = findObjectById(registro, parentId);
        if (!padre) {
            registrarErrorOperacional("not_found", "Objeto padre no encontrado: " + std::to_string(parentId));
            return ResultadoComando::fallo("not_found", "Objeto padre no encontrado",
                                          codigosSalida::kRecursoNoEncontrado);
        }
    } else {
        padre = registro->getRoot();
    }

    if (hijo == padre) {
        registrarErrorUso("invalid_request", "Un objeto no puede ser padre de sí mismo");
        return ResultadoComando::fallo("invalid_request", "Objeto mismo como padre",
                                      codigosSalida::kErrorUso);
    }

    Entity* padreAnteriorEnt = hijo->getParentEntity();
    GameObject* padreAnteriorGo = dynamic_cast<GameObject*>(padreAnteriorEnt); int padreAnteriorId = padreAnteriorGo ? padreAnteriorGo->getId() : 0;

    if (!registro->reparent(hijo, padre)) {
        registrarErrorOperacional("reparent_failed", "No se pudo reparentar el objeto id=" + std::to_string(id));
        return ResultadoComando::fallo("reparent_failed", "No se pudo reparentar el objeto",
                                      codigosSalida::kFalloGeneral);
    }

    hijo->setDirty(true);
    if (padre) padre->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "setparent", id,
                           nlohmann::json{{"parentId", padreAnteriorId}},
                           nlohmann::json{{"parentId", parentId}});

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"parentId", parentId},
        {"reparented", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.create
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoCrear(const nlohmann::json& argumentos) {
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
    int parentId = 0;
    if (argumentos.contains("parentId") && argumentos["parentId"].is_number_unsigned()) {
        parentId = static_cast<int>(argumentos["parentId"].get<unsigned>());
    }

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* padre = nullptr;
    if (parentId != 0) {
        padre = findObjectById(registro, parentId);
        if (!padre) {
            padre = registro->getRoot();
        }
    } else {
        padre = registro->getRoot();
    }

    auto* objeto = new GameObject();
    objeto->setTag("Untagged");
    // inputName tiene 25 chars, copiar con trunca
    strncpy(objeto->inputName, nombre.c_str(), sizeof(objeto->inputName) - 1);
    objeto->inputName[sizeof(objeto->inputName) - 1] = '\0';

    GameObject* creado = registro->createObject(
        std::unique_ptr<GameObject>(objeto), padre);

    if (!creado) {
        registrarErrorOperacional("create_failed", "No se pudo crear el objeto");
        return ResultadoComando::fallo("create_failed", "No se pudo crear el objeto",
                                      codigosSalida::kFalloGeneral);
    }

    creado->setDirty(true);
    if (padre) padre->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "create", creado->getId(),
                           nlohmann::json{},
                           nlohmann::json{{"name", nombre}, {"parentId", parentId}});

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", creado->getId()},
        {"name", creado->inputName},
        {"parentId", parentId},
        {"created", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.unparent
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoDesvincularPadre(const nlohmann::json& argumentos) {
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

    Entity* padreEnt = obj->getParentEntity();
    GameObject* padre = dynamic_cast<GameObject*>(padreEnt);
    if (!padre) {
        return ResultadoComando::exito({
            {"project", nombreProyecto},
            {"id", id},
            {"alreadyRoot", true}
        });
    }

    GameObject* raiz = registro->getRoot();
    if (!raiz) {
        registrarErrorOperacional("root_missing", "Escena sin root");
        return ResultadoComando::fallo("root_missing", "Escena sin root",
                                      codigosSalida::kFalloGeneral);
    }

    int padreAnteriorId = padre ? padre->getId() : 0;

    // Desvincular del padre actual y volver a root
    registro->reparent(obj, raiz);

    obj->setDirty(true);
    if (raiz) raiz->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "unparent", id,
                           nlohmann::json{{"parentId", padreAnteriorId}},
                           nlohmann::json{{"parentId", 0}});

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"unparented", true}
    });
}

}  // namespace funshi
