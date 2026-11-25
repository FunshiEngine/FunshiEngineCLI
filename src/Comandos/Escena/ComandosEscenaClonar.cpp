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
// scene.object.clone.
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
// scene.object.clone
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoClonar(const nlohmann::json& argumentosIn) {
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

    int idOrigen = static_cast<int>(argumentos["id"].get<unsigned>());
    int parentIdDestino = 0;
    if (argumentos.contains("parentId") && argumentos["parentId"].is_number_unsigned()) {
        parentIdDestino = static_cast<int>(argumentos["parentId"].get<unsigned>());
    }

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* origen = findObjectById(registro, idOrigen);
    if (!origen) {
        registrarErrorUso("object_not_found", "Objeto origen no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto origen no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    GameObject* padreDestino = registro->getRoot();
    if (parentIdDestino != 0) {
        GameObject* candidato = findObjectById(registro, parentIdDestino);
        if (candidato) padreDestino = candidato;
    }

    // Clonar con nuevo id y los mismos componentes
    auto* clon = new GameObject();
    clon->setTag(origen->getTag());
    strncpy(clon->inputName, origen->inputName, sizeof(clon->inputName) - 1);
    clon->inputName[sizeof(clon->inputName) - 1] = '\0';
    // Copiar transform
    Transform* transformOrigen = origen->getComponent<Transform>();
    if (transformOrigen) {
        Transform* transformClon = clon->getComponent<Transform>();
        if (transformClon) {
            transformClon->setTranslatef(transformOrigen->getTranslatef()[0],
                                         transformOrigen->getTranslatef()[1],
                                         transformOrigen->getTranslatef()[2]);
            transformClon->setRotatef(transformOrigen->getRotatef()[0],
                                      transformOrigen->getRotatef()[1],
                                      transformOrigen->getRotatef()[2],
                                      transformOrigen->getRotatef()[3]);
            transformClon->setScalef(transformOrigen->getScalef()[0],
                                     transformOrigen->getScalef()[1],
                                     transformOrigen->getScalef()[2]);
        }
    }

    GameObject* creado = registro->createObject(
        std::unique_ptr<GameObject>(clon), padreDestino);

    if (!creado) {
        registrarErrorOperacional("clone_failed", "No se pudo clonar el objeto");
        return ResultadoComando::fallo("clone_failed", "No se pudo clonar el objeto",
                                      codigosSalida::kFalloGeneral);
    }

    // Clonar componentes del origen (menos Transform, ya existe)
    ListaDE<Component*>* compsOrigen = origen->getComponents();
    if (compsOrigen && !compsOrigen->isEmpty()) {
        Position<Component*>* pos = compsOrigen->first();
        while (pos != nullptr) {
            Component* compRaw = pos->getElement();
            if (dynamic_cast<Transform*>(compRaw)) {
                pos = (pos != compsOrigen->last()) ? compsOrigen->next(pos) : nullptr;
                continue;
            }
            const std::string& tipo = tipoComponenteNombre(compRaw);

            if (tipo == "Script") {
                Script* scriptOrig = dynamic_cast<Script*>(compRaw);
                if (scriptOrig) {
                    auto* scriptClon = new Script();
                    scriptClon->setDllPath(scriptOrig->getPath());
                    creado->addComponent(std::unique_ptr<Component>(scriptClon));
                }
            } else if (tipo == "Model") {
                Model* modelOrig = dynamic_cast<Model*>(compRaw);
                if (modelOrig) {
                    auto* modelClon = new Model();
                    modelClon->setPath(modelOrig->getPath());
                    creado->addComponent(std::unique_ptr<Component>(modelClon));
                }
            } else if (tipo == "Color") {
                Color* colorOrig = dynamic_cast<Color*>(compRaw);
                if (colorOrig) {
                    auto* colorClon = new Color();
                    colorClon->setColor(colorOrig->getColor());
                    creado->addComponent(std::unique_ptr<Component>(colorClon));
                }
            } else if (tipo == "AudioSource") {
                AudioSource* audioOrig = dynamic_cast<AudioSource*>(compRaw);
                if (audioOrig) {
                    auto* audioClon = new AudioSource();
                    audioClon->setRutaClip(audioOrig->getRutaClip());
                    audioClon->setReproduccionAutomatica(audioOrig->isReproduccionAutomatica());
                    audioClon->setEspacial(audioOrig->isEspacial());
                    creado->addComponent(std::unique_ptr<Component>(audioClon));
                }
            } else if (tipo == "Collider") {
                Collider* colliderOrig = dynamic_cast<Collider*>(compRaw);
                if (colliderOrig) {
                    Transform* t = creado->getComponent<Transform>();
                    if (t) {
                        auto* colliderClon = new EsfereCollider(colliderOrig->getRadio(), t, creado);
                        creado->addComponent(std::unique_ptr<Component>(colliderClon));
                    }
                }
            } else if (tipo == "RigidBody") {
                RigidBody* rbOrig = dynamic_cast<RigidBody*>(compRaw);
                if (rbOrig) {
                    Collider* colliderClon = creado->getComponent<Collider>();
                    if (!colliderClon) {
                        Transform* t = creado->getComponent<Transform>();
                        if (t) {
                            colliderClon = new EsfereCollider(1.0f, t, creado);
                            creado->addComponent(std::unique_ptr<Component>(colliderClon));
                        }
                    }
                    if (colliderClon) {
                        auto* rbClon = new RigidBody(colliderClon, rbOrig->masa());
                        creado->addComponent(std::unique_ptr<Component>(rbClon));
                    }
                }
            }
            pos = (pos != compsOrigen->last()) ? compsOrigen->next(pos) : nullptr;
        }
    }

    creado->setDirty(true);
    if (padreDestino) padreDestino->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "clone", creado->getId(),
                           nlohmann::json{{"sourceId", idOrigen}},
                           nlohmann::json{{"name", creado->inputName}, {"parentId", padreDestino->getId()}});

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", creado->getId()},
        {"name", creado->inputName},
        {"parentId", padreDestino->getId()},
        {"cloned", true}
    });
}

}  // namespace funshi
