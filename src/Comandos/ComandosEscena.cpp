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
#include "Comandos/ComandosEscena.h"

#include <Configuracion/ProjectManager.h>
#include <Configuracion/ProjectPaths.h>

#include "Escena/ContextoEscena.h"
#include "Escena/SerializacionEscena.h"
#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <Objetos/Componentes/Transform.h>
#include <Objetos/Componentes/Model.h>
#include <Objetos/Componentes/Script.h>
#include <Objetos/Componentes/Color.h>
#include <Objetos/Componentes/AudioSource.h>
#include <Objetos/Componentes/Colliders/EsfereCollider.h>
#include <Objetos/Componentes/Colliders/CubeCollider.h>
#include <Objetos/Componentes/RigidBody/RigidBody.h>

#include <filesystem>

namespace funshi {
// ---------------------------------------------------------------------------
// Helper: obtiene el nombre legible del tipo de un componente usando dynamic_cast
// ---------------------------------------------------------------------------
static std::string tipoComponenteNombre(Component* comp) {
    if (dynamic_cast<Transform*>(comp)) return "Transform";
    if (dynamic_cast<Script*>(comp)) return "Script";
    if (dynamic_cast<Model*>(comp)) return "Model";
    if (dynamic_cast<Color*>(comp)) return "Color";
    if (dynamic_cast<AudioSource*>(comp)) return "AudioSource";
    if (dynamic_cast<Collider*>(comp)) return "Collider";
    if (dynamic_cast<RigidBody*>(comp)) return "RigidBody";
    return "Unknown";
}


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

ResultadoComando escenaObjetoPosicionGlobal(const nlohmann::json& argumentos) {
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

ResultadoComando escenaObjetoSetTransform(const nlohmann::json& argumentos) {
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

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", obj->getId()},
        {"name", obj->inputName},
        {"modified", true}
    });
}

ResultadoComando escenaObjetoSetComponente(const nlohmann::json& argumentos) {
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

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", obj->getId()},
        {"name", obj->inputName},
        {"componentType", componentType},
        {"field", field},
        {"modified", true}
    });
}

ResultadoComando escenaGuardar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);
    const std::string dirScene = ProjectPaths::rutaSceneDir(nombreProyecto);
    contexto.serializer().save(dirScene);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"saved", true}
    });
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

    if (!registro->reparent(hijo, padre)) {
        registrarErrorOperacional("reparent_failed", "No se pudo reparentar el objeto id=" + std::to_string(id));
        return ResultadoComando::fallo("reparent_failed", "No se pudo reparentar el objeto",
                                      codigosSalida::kFalloGeneral);
    }

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"parentId", parentId},
        {"reparented", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.addcomponent
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoAddComponente(const nlohmann::json& argumentos) {
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

    unsigned idU = argumentos["id"].get<unsigned>(); int id = static_cast<int>(idU);
    std::string componentType = argumentos["componentType"].get<std::string>();

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
        if (!argumentos.contains("value") || !argumentos["value"].is_object()) {
            registrarErrorUso("invalid_request", "Collider requiere campo 'value' con {tipo, radio}");
            return ResultadoComando::fallo("invalid_request", "Falta configuracion de Collider",
                                          codigosSalida::kErrorUso);
        }
        const auto& val = argumentos["value"];
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

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"componentType", componentType},
        {"added", true}
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

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", creado->getId()},
        {"name", creado->inputName},
        {"parentId", parentId},
        {"created", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.clone
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoClonar(const nlohmann::json& argumentos) {
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

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", creado->getId()},
        {"name", creado->inputName},
        {"parentId", padreDestino->getId()},
        {"cloned", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.removecomponent
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoQuitarComponente(const nlohmann::json& argumentos) {
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

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"componentType", componentType},
        {"removed", true}
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

    // Desvincular del padre actual y volver a root
    registro->reparent(obj, raiz);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"unparented", true}
    });
}
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

    obj->setTag(tag);

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

    obj->setState(activo);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", id},
        {"active", activo},
        {"activated", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.components
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoComponentes(const nlohmann::json& argumentos) {
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
