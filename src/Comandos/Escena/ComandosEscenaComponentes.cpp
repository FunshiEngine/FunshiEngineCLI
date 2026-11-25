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
// Alta, baja y listado de componentes de un objeto.
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
// scene.object.addcomponent
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoAddComponente(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"id", true, nlohmann::json::value_t::number_unsigned},
        {"componentType", true, nlohmann::json::value_t::string},
        {"loop", false, nlohmann::json::value_t::boolean},
        {"autoplay", false, nlohmann::json::value_t::boolean},
        {"espacial", false, nlohmann::json::value_t::boolean},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    unsigned idU = argumentos["id"].get<unsigned>(); int id = static_cast<int>(idU);
    std::string componentType = argumentos["componentType"].get<std::string>();

    for (const char* clave : {"volume", "masa"}) {
        if (argumentos.contains(clave) && !argumentos[clave].is_number()) {
            const std::string mensaje = std::string("args.") + clave + " debe ser un número";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje,
                                          codigosSalida::kErrorUso);
        }
    }

    ContextoEscena contexto(nombreProyecto);
    GameObject* obj = findObjectById(&contexto.registro(), id);
    if (!obj) {
        registrarErrorOperacional("not_found", "Objeto no encontrado: " + std::to_string(id));
        return ResultadoComando::fallo("not_found", "Objeto no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    if (componentType == "Script") {
        if (!argumentos.contains("value") || !argumentos["value"].is_string()) {
            registrarErrorUso("invalid_request", "Script requiere campo 'value' con scriptPath");
            return ResultadoComando::fallo("invalid_request", "Falta scriptPath",
                                          codigosSalida::kErrorUso);
        }
        auto* script = new Script();
        script->setDllPath(argumentos["value"].get<std::string>());
        obj->addComponent(std::unique_ptr<Component>(script));
    } else if (componentType == "Model") {
        if (!argumentos.contains("value") || !argumentos["value"].is_string()) {
            registrarErrorUso("invalid_request", "Model requiere campo 'value' con modelPath");
            return ResultadoComando::fallo("invalid_request", "Falta modelPath",
                                          codigosSalida::kErrorUso);
        }
        auto* model = new Model();
        model->setPath(argumentos["value"].get<std::string>());
        obj->addComponent(std::unique_ptr<Component>(model));
    } else if (componentType == "Color") {
        auto* color = new Color();
        if (argumentos.contains("value") && argumentos["value"].is_array()) {
            const auto& arr = argumentos["value"];
            if (arr.size() >= 3) {
                color->setColor(static_cast<float>(arr[0].get<double>()),
                                static_cast<float>(arr[1].get<double>()),
                                static_cast<float>(arr[2].get<double>()));
            }
        }
        obj->addComponent(std::unique_ptr<Component>(color));
    } else if (componentType == "AudioSource") {
        auto* audio = new AudioSource();
        if (argumentos.contains("value") && argumentos["value"].is_string()) {
            audio->setRutaClip(argumentos["value"].get<std::string>());
        }
        if (argumentos.contains("volume") && argumentos["volume"].is_number()) {
            audio->setVolume(static_cast<float>(argumentos["volume"].get<double>()));
        }
        audio->setLoop(argumentos.value("loop", false));
        audio->setReproduccionAutomatica(argumentos.value("autoplay", false));
        audio->setEspacial(argumentos.value("espacial", false));
        obj->addComponent(std::unique_ptr<Component>(audio));
    } else if (componentType == "Collider") {
        nlohmann::json val = nlohmann::json::object();
        if (argumentos.contains("value") && argumentos["value"].is_object()) {
            val = argumentos["value"];
        } else if (argumentos.contains("value") && argumentos["value"].is_string()) {
            // Tolerar "esfera"/"cubo" o JSON serializado simple.
            const std::string s = argumentos["value"].get<std::string>();
            if (s == "esfera" || s == "cubo") val["tipo"] = s;
        }
        std::string tipo = val.value("tipo", std::string("esfera"));
        float radio = static_cast<float>(val.value("radio", 1.0));
        Transform* transform = obj->getComponent<Transform>();
        if (!transform) {
            registrarErrorUso("invalid_request", "El objeto no tiene Transform; no se puede agregar Collider");
            return ResultadoComando::fallo("invalid_request", "Falta Transform",
                                          codigosSalida::kErrorUso);
        }
        Collider* collider = nullptr;
        if (tipo == "cubo") {
            collider = new CubeCollider(radio, transform, obj);
        } else {
            collider = new EsfereCollider(radio, transform, obj);
        }
        obj->addComponent(std::unique_ptr<Component>(collider));
    } else if (componentType == "RigidBody") {
        float masa = static_cast<float>(argumentos.value("masa", 1.0));
        Collider* collider = obj->getComponent<Collider>();
        if (!collider) {
            Transform* transform = obj->getComponent<Transform>();
            if (!transform) {
                registrarErrorUso("invalid_request", "El objeto no tiene Transform; no se puede agregar RigidBody");
                return ResultadoComando::fallo("invalid_request", "Falta Transform",
                                              codigosSalida::kErrorUso);
            }
            collider = new EsfereCollider(1.0f, transform, obj);
            obj->addComponent(std::unique_ptr<Component>(collider));
        }
        auto* rigidBody = new RigidBody(collider, masa);
        obj->addComponent(std::unique_ptr<Component>(rigidBody));
    } else {
        std::string mensaje = "Tipo de componente no soportado para agregar: " + componentType;
        registrarErrorUso("unsupported", mensaje);
        return ResultadoComando::fallo("unsupported", mensaje, codigosSalida::kErrorUso);
    }

    obj->setDirty(true);

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"componentType", componentType},
        {"added", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.removecomponent
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoQuitarComponente(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"id", true, nlohmann::json::value_t::number_unsigned},
        {"componentType", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    int id = static_cast<int>(argumentos["id"].get<unsigned>());
    std::string componentType = argumentos["componentType"].get<std::string>();

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* obj = findObjectById(registro, id);
    if (!obj) {
        registrarErrorUso("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    // Buscar el componente por tipo y eliminarlo
    bool eliminado = false;
    ListaDE<Component*>* componentes = obj->getComponents();
    if (componentes && !componentes->isEmpty()) {
        Position<Component*>* pos = componentes->first();
        while (pos != nullptr) {
            Component* comp = pos->getElement();
            if (tipoComponenteNombre(comp) == componentType) {
                obj->deleteComponent(comp);
                eliminado = true;
                break;
            }
            pos = (pos != componentes->last()) ? componentes->next(pos) : nullptr;
        }
    }

    if (!eliminado) {
        registrarErrorUso("component_not_found",
                                   "Componente no encontrado en el objeto");
        return ResultadoComando::fallo("component_not_found",
                                      "Componente no encontrado en el objeto",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    obj->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "removecomponent", id,
                           nlohmann::json{{"componentType", componentType}},
                           nlohmann::json{{"componentType", componentType}});

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"componentType", componentType},
        {"removed", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.components
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoComponentes(const nlohmann::json& argumentosIn) {
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

    nlohmann::json listaComponentes = nlohmann::json::array();
    ListaDE<Component*>* componentesObj = obj->getComponents();
    if (componentesObj && !componentesObj->isEmpty()) {
        Position<Component*>* pos = componentesObj->first();
        while (pos != nullptr) {
            Component* comp = pos->getElement();
            nlohmann::json entry;
            entry["type"] = tipoComponenteNombre(comp);
            listaComponentes.push_back(entry);
            pos = (pos != componentesObj->last()) ? componentesObj->next(pos) : nullptr;
        }
    }

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"components", listaComponentes}
    });
}

}  // namespace funshi
