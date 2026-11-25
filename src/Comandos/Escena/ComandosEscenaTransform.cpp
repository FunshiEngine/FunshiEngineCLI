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
// scene.object.settransform y scene.object.setcomponent.
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

ResultadoComando escenaObjetoSetTransform(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);

    GameObject* obj = nullptr;
    if (argumentos.contains("id") && (argumentos["id"].is_number_integer() || argumentos["id"].is_number_unsigned() || argumentos["id"].is_string())) {
        try {
            int id = argumentos["id"].is_string() ? std::stoi(argumentos["id"].get<std::string>()) : argumentos["id"].get<int>();
            obj = findObjectById(&contexto.registro(), id);
        } catch (...) {
            const std::string mensaje = "args.id debe ser un entero";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
        }
    } else if (argumentos.contains("tag") && argumentos["tag"].is_string()) {
        obj = findObjectByTag(&contexto.registro(), argumentos["tag"].get<std::string>());
    } else {
        const std::string mensaje = "args.id (entero) o args.tag (string) es requerido";
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
    }

    if (!obj) {
        registrarErrorOperacional("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                       codigosSalida::kRecursoNoEncontrado);
    }

    Transform* t = obj->getComponent<Transform>();
    if (!t) {
        registrarErrorOperacional("no_transform", "El objeto no tiene componente Transform");
        return ResultadoComando::fallo("no_transform", "El objeto no tiene componente Transform",
                                       codigosSalida::kRecursoNoEncontrado);
    }

    bool modificado = false;

    if (argumentos.contains("position") && argumentos["position"].is_array()) {
        const auto& arr = argumentos["position"];
        if (arr.size() >= 3) {
            t->setTranslatef(static_cast<float>(arr[0].get<double>()),
                             static_cast<float>(arr[1].get<double>()),
                             static_cast<float>(arr[2].get<double>()));
            modificado = true;
        }
    }

    if (argumentos.contains("rotation") && argumentos["rotation"].is_array()) {
        const auto& arr = argumentos["rotation"];
        if (arr.size() >= 4) {
            t->setRotatef(static_cast<float>(arr[0].get<double>()),
                          static_cast<float>(arr[1].get<double>()),
                          static_cast<float>(arr[2].get<double>()),
                          static_cast<float>(arr[3].get<double>()));
            modificado = true;
        }
    }

    if (argumentos.contains("scale") && argumentos["scale"].is_array()) {
        const auto& arr = argumentos["scale"];
        if (arr.size() >= 3) {
            t->setScalef(static_cast<float>(arr[0].get<double>()),
                         static_cast<float>(arr[1].get<double>()),
                         static_cast<float>(arr[2].get<double>()));
            modificado = true;
        }
    }

    if (!modificado) {
        const std::string mensaje = "Se requiere al menos uno de: position, rotation, scale";
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
    }

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", obj->getId()},
        {"name", obj->inputName},
        {"modified", true}
    });
}

ResultadoComando escenaObjetoSetComponente(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"componentType", true, nlohmann::json::value_t::string},
        {"field", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion, codigosSalida::kErrorUso);
    }

    const std::string componentType = argumentos["componentType"].get<std::string>();
    const std::string field = argumentos["field"].get<std::string>();

    ContextoEscena contexto(nombreProyecto);

    GameObject* obj = nullptr;
    if (argumentos.contains("id") && (argumentos["id"].is_number_integer() || argumentos["id"].is_number_unsigned() || argumentos["id"].is_string())) {
        try {
            int id = argumentos["id"].is_string() ? std::stoi(argumentos["id"].get<std::string>()) : argumentos["id"].get<int>();
            obj = findObjectById(&contexto.registro(), id);
        } catch (...) {
            const std::string mensaje = "args.id debe ser un entero";
            registrarErrorUso("invalid_request", mensaje);
            return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
        }
    } else if (argumentos.contains("tag") && argumentos["tag"].is_string()) {
        obj = findObjectByTag(&contexto.registro(), argumentos["tag"].get<std::string>());
    } else {
        const std::string mensaje = "args.id (entero) o args.tag (string) es requerido";
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
    }

    if (!obj) {
        registrarErrorOperacional("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                       codigosSalida::kRecursoNoEncontrado);
    }

    bool modificado = false;

    if (componentType == "Transform") {
        if (auto* t = obj->getComponent<Transform>()) {
            if (field == "position") {
                if (argumentos.contains("value") && argumentos["value"].is_array()) {
                    const auto& arr = argumentos["value"];
                    if (arr.size() >= 3) {
                        t->setTranslatef(static_cast<float>(arr[0].get<double>()),
                                         static_cast<float>(arr[1].get<double>()),
                                         static_cast<float>(arr[2].get<double>()));
                        modificado = true;
                    }
                }
            } else if (field == "rotation") {
                if (argumentos.contains("value") && argumentos["value"].is_array()) {
                    const auto& arr = argumentos["value"];
                    if (arr.size() >= 4) {
                        t->setRotatef(static_cast<float>(arr[0].get<double>()),
                                      static_cast<float>(arr[1].get<double>()),
                                      static_cast<float>(arr[2].get<double>()),
                                      static_cast<float>(arr[3].get<double>()));
                        modificado = true;
                    }
                }
            } else if (field == "scale") {
                if (argumentos.contains("value") && argumentos["value"].is_array()) {
                    const auto& arr = argumentos["value"];
                    if (arr.size() >= 3) {
                        t->setScalef(static_cast<float>(arr[0].get<double>()),
                                     static_cast<float>(arr[1].get<double>()),
                                     static_cast<float>(arr[2].get<double>()));
                        modificado = true;
                    }
                }
            }
        }
    } else if (componentType == "Model") {
        if (auto* m = obj->getComponent<Model>()) {
            if (field == "modelPath") {
                if (argumentos.contains("value") && argumentos["value"].is_string()) {
                    std::string pathValue = argumentos["value"].get<std::string>();
                    bool existe = false;
                    if (std::filesystem::path(pathValue).is_absolute()) {
                        existe = std::filesystem::exists(pathValue);
                    } else {
                        std::string rutaCompleta = ProjectPaths::directorioSrc(nombreProyecto) + "/" + pathValue;
                        existe = std::filesystem::exists(rutaCompleta);
                    }
                    if (!existe) {
                        registrarErrorOperacional("asset_not_found", "El asset no existe: " + pathValue);
                    }
                    m->setPath(pathValue);
                    modificado = true;
                }
            }
        }
    } else if (componentType == "Script") {
        if (auto* s = obj->getComponent<Script>()) {
            if (field == "scriptPath") {
                if (argumentos.contains("value") && argumentos["value"].is_string()) {
                    std::string pathValue = argumentos["value"].get<std::string>();
                    bool existe = false;
                    if (std::filesystem::path(pathValue).is_absolute()) {
                        existe = std::filesystem::exists(pathValue);
                    } else {
                        std::string rutaCompleta = ProjectPaths::directorioSrc(nombreProyecto) + "/" + pathValue;
                        existe = std::filesystem::exists(rutaCompleta);
                    }
                    if (!existe) {
                        registrarErrorOperacional("asset_not_found", "El asset no existe: " + pathValue);
                    }
                    s->setDllPath(pathValue);
                    modificado = true;
                }
            }
        }
    } else if (componentType == "Collider") {
        if (auto* col = obj->getComponent<Collider>()) {
            auto leerFloat = [&](float& out) -> bool {
                if (!argumentos.contains("value")) return false;
                const auto& v = argumentos["value"];
                if (v.is_number()) { out = static_cast<float>(v.get<double>()); return true; }
                if (v.is_string()) {
                    try { out = std::stof(v.get<std::string>()); return true; } catch (...) {}
                }
                return false;
            };
            if (field == "radio") {
                float r = 1.0f;
                if (leerFloat(r)) { col->setRadio(r); modificado = true; }
            } else if (field == "tipo") {
                if (argumentos.contains("value") && argumentos["value"].is_string()) {
                    std::string tipo = argumentos["value"].get<std::string>();
                    Transform* t = obj->getComponent<Transform>();
                    if (t) {
                        Collider* nuevo = nullptr;
                        if (tipo == "cubo") nuevo = new CubeCollider(col->getRadio(), t, obj);
                        else nuevo = new EsfereCollider(col->getRadio(), t, obj);
                        obj->addComponent(std::unique_ptr<Component>(nuevo));
                        modificado = true;
                    }
                }
            }
        }
    } else if (componentType == "RigidBody") {
        if (auto* rb = obj->getComponent<RigidBody>()) {
            auto leerFloat = [&](float& out) -> bool {
                if (!argumentos.contains("value")) return false;
                const auto& v = argumentos["value"];
                if (v.is_number()) { out = static_cast<float>(v.get<double>()); return true; }
                if (v.is_string()) {
                    try { out = std::stof(v.get<std::string>()); return true; } catch (...) {}
                }
                return false;
            };
            if (field == "masa") {
                float masa = 1.0f;
                if (leerFloat(masa)) { rb->fijarMasa(masa); modificado = true; }
            } else if (field == "gravedad") {
                if (argumentos.contains("value") && argumentos["value"].is_boolean()) {
                    rb->fijarUsoGravedad(argumentos["value"].get<bool>());
                    modificado = true;
                } else if (argumentos.contains("value") && argumentos["value"].is_string()) {
                    const auto sv = argumentos["value"].get<std::string>();
                    rb->fijarUsoGravedad(sv == "true" || sv == "1");
                    modificado = true;
                }
            }
        }
    } else if (componentType == "Color") {
        if (auto* c = obj->getComponent<Color>()) {
            if (field == "value" && argumentos.contains("value") && argumentos["value"].is_array()) {
                const auto& arr = argumentos["value"];
                if (arr.size() >= 4) {
                    c->setColor(static_cast<float>(arr[0].get<double>()),
                                static_cast<float>(arr[1].get<double>()),
                                static_cast<float>(arr[2].get<double>()));
                    // Alpha: Color no expone setAlpha; se ignora si no hay API.
                    modificado = true;
                }
            }
        }
    } else if (componentType == "AudioSource") {
        if (auto* a = obj->getComponent<AudioSource>()) {
            auto leerFloat = [&](float& out) -> bool {
                if (!argumentos.contains("value")) return false;
                const auto& v = argumentos["value"];
                if (v.is_number()) { out = static_cast<float>(v.get<double>()); return true; }
                if (v.is_string()) {
                    try { out = std::stof(v.get<std::string>()); return true; } catch (...) {}
                }
                return false;
            };
            if (field == "clip" && argumentos.contains("value") && argumentos["value"].is_string()) {
                a->setRutaClip(argumentos["value"].get<std::string>()); modificado = true;
            } else if (field == "volume") {
                float vol = 1.0f; if (leerFloat(vol)) { a->setVolume(vol); modificado = true; }
            } else if (field == "loop" && argumentos.contains("value")) {
                const auto& v = argumentos["value"];
                bool b = v.is_boolean() ? v.get<bool>() : (v.is_string() && v.get<std::string>() == "true");
                a->setLoop(b); modificado = true;
            } else if (field == "autoplay" && argumentos.contains("value")) {
                const auto& v = argumentos["value"];
                bool b = v.is_boolean() ? v.get<bool>() : (v.is_string() && v.get<std::string>() == "true");
                a->setReproduccionAutomatica(b); modificado = true;
            } else if (field == "espacial" && argumentos.contains("value")) {
                const auto& v = argumentos["value"];
                bool b = v.is_boolean() ? v.get<bool>() : (v.is_string() && v.get<std::string>() == "true");
                a->setEspacial(b); modificado = true;
            }
        }
    } else {
        std::string mensaje = "Tipo de componente no soportado: " + componentType;
        registrarErrorUso("unsupported", mensaje);
        return ResultadoComando::fallo("unsupported", mensaje, codigosSalida::kErrorUso);
    }

    if (!modificado) {
        std::string mensaje = "No se pudo establecer el campo '" + field +
                              "' en el componente '" + componentType + "'";
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
    }

    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", obj->getId()},
        {"name", obj->inputName},
        {"componentType", componentType},
        {"field", field},
        {"modified", true}
    });
}

}  // namespace funshi
