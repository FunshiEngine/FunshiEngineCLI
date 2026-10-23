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
#include <Assets/AssetPath.h>

#include "Escena/ContextoEscena.h"
#include "Escena/SerializacionEscena.h"
#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <Objetos/Componentes/Transform.h>
#include <Objetos/Componentes/Model.h>
#include <Objetos/Componentes/Script.h>
#include <Behaviour/ScriptRuntime.h>
#include <Behaviour/Reflection/BehaviourReflection.h>
#include <Objetos/Componentes/Color.h>
#include <Objetos/Componentes/AudioSource.h>
#include <Objetos/Componentes/Colliders/EsfereCollider.h>
#include <Objetos/Componentes/Colliders/CubeCollider.h>
#include <Objetos/Componentes/RigidBody/RigidBody.h>

#include <filesystem>
#include <fstream>

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

// Forward declarations: definidas despues en este archivo.
void registrarOperacionUndo(const std::string& nombreProyecto,
                            const std::string& tipo,
                            int idObjeto,
                            const nlohmann::json& antes,
                            const nlohmann::json& despues);
bool cargarHistorial(const std::string& ruta, nlohmann::json& historial);
bool guardarHistorial(const std::string& ruta, const nlohmann::json& historial);
std::string rutaHistorialUndo(const std::string& nombreProyecto) {
    return ProjectPaths::directorioMemory(nombreProyecto) + "/undo_history.json";
}

bool cargarHistorial(const std::string& ruta, nlohmann::json& historial) {
    std::ifstream f(ruta);
    if (!f.is_open()) {
        historial = nlohmann::json::array();
        return true;
    }
    try {
        f >> historial;
        return true;
    } catch (...) {
        historial = nlohmann::json::array();
        return true;
    }
}

bool guardarHistorial(const std::string& ruta, const nlohmann::json& historial) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(ruta).parent_path(), ec);
    std::ofstream f(ruta);
    if (!f.is_open()) return false;
    f << historial.dump(2);
    return f.good();
}

void registrarOperacionUndo(const std::string& nombreProyecto,
                            const std::string& tipo,
                            int idObjeto,
                            const nlohmann::json& antes,
                            const nlohmann::json& despues) {
    std::string ruta = rutaHistorialUndo(nombreProyecto);
    nlohmann::json historial;
    if (!cargarHistorial(ruta, historial)) return;

    nlohmann::json operacion;
    operacion["type"] = tipo;
    operacion["id"] = idObjeto;
    operacion["before"] = antes;
    operacion["after"] = despues;
    historial.push_back(operacion);

    // Mantener solo las últimas 100 operaciones
    if (historial.size() > 100)
        while (historial.size() > 100) { historial.erase(historial.begin()); };

    guardarHistorial(ruta, historial);
}

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

    obj->setDirty(true);

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

    creado->setDirty(true);
    if (padre) padre->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "create", creado->getId(),
                           nlohmann::json{},
                           nlohmann::json{{"name", nombre}, {"parentId", parentId}});

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

    creado->setDirty(true);
    if (padreDestino) padreDestino->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "clone", creado->getId(),
                           nlohmann::json{{"sourceId", idOrigen}},
                           nlohmann::json{{"name", creado->inputName}, {"parentId", padreDestino->getId()}});

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

    obj->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "removecomponent", id,
                           nlohmann::json{{"componentType", componentType}},
                           nlohmann::json{{"componentType", componentType}});
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

    std::string tagAnterior = obj->getTag();
    obj->setTag(tag);
    obj->setDirty(true);

    registrarOperacionUndo(nombreProyecto, "settag", id,
                           nlohmann::json{{"tag", tagAnterior}},
                           nlohmann::json{{"tag", tag}});

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
// ---------------------------------------------------------------------------
// scene.object.getglobaltransform
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoGetGlobalTransform(const nlohmann::json& argumentos) {
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
// ---------------------------------------------------------------------------
// scene.script.compile
// ---------------------------------------------------------------------------
ResultadoComando escenaScriptCompilar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    bool soloUno = false;
    int idObjeto = 0;
    if (argumentos.contains("id") && argumentos["id"].is_number_unsigned()) {
        idObjeto = static_cast<int>(argumentos["id"].get<unsigned>());
        soloUno = true;
    }

    if (soloUno) {
        ContextoEscena contexto(nombreProyecto);
        SceneRegistry* registro = &contexto.registro();
        GameObject* obj = findObjectById(registro, idObjeto);
        if (!obj) {
            registrarErrorUso("object_not_found", "Objeto no encontrado");
            return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                          codigosSalida::kRecursoNoEncontrado);
        }
        Script* script = obj->getComponent<Script>();
        if (!script) {
            registrarErrorUso("component_not_found", "El objeto no tiene componente Script");
            return ResultadoComando::fallo("component_not_found",
                                          "El objeto no tiene componente Script",
                                          codigosSalida::kRecursoNoEncontrado);
        }
        std::string fuente = script->getPath();
        if (fuente.empty()) {
            registrarErrorUso("invalid_request", "El script no tiene ruta de fuente asignada");
            return ResultadoComando::fallo("invalid_request",
                                          "El script no tiene ruta de fuente asignada",
                                          codigosSalida::kErrorUso);
        }
        ComportamientoCargado comportamiento;
        std::string compileError;
        std::string className = script->getNameClass();
        if (className.empty()) {
            size_t pos = fuente.find_last_of("/\\");
            std::string nombreArchivo = (pos == std::string::npos) ? fuente : fuente.substr(pos + 1);
            size_t punto = nombreArchivo.find_last_of('.');
            className = (punto == std::string::npos) ? nombreArchivo : nombreArchivo.substr(0, punto);
        }
        if (ScriptRuntime::compilarYCargar(fuente, className, comportamiento, compileError)) {
            script->setDllPath(fuente);
            script->setNombreComponente(className);
            return ResultadoComando::exito({
                {"project", nombreProyecto},
                {"id", idObjeto},
                {"scriptPath", fuente},
                {"compiled", true},
                {"message", "Script compilado y cargado correctamente"}
            });
        } else {
            return ResultadoComando::fallo("compile_error", compileError,
                                          codigosSalida::kFalloGeneral);
        }
    } else {
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
}
// ---------------------------------------------------------------------------
// scene.script.reload
// ---------------------------------------------------------------------------
ResultadoComando escenaScriptRecargar(const nlohmann::json& argumentos) {
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

    int idObjeto = static_cast<int>(argumentos["id"].get<unsigned>());

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* obj = findObjectById(registro, idObjeto);
    if (!obj) {
        registrarErrorUso("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    Script* script = obj->getComponent<Script>();
    if (!script) {
        registrarErrorUso("component_not_found", "El objeto no tiene componente Script");
        return ResultadoComando::fallo("component_not_found",
                                      "El objeto no tiene componente Script",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    if (!script->estaCargado()) {
        return ResultadoComando::exito({
            {"project", nombreProyecto},
            {"id", idObjeto},
            {"reloaded", false},
            {"message", "El script no estaba cargado; no hay nada que recargar"}
        });
    }

    script->recargar(obj);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", idObjeto},
        {"scriptPath", script->getPath()},
        {"reloaded", true},
        {"message", "Script recargado correctamente"}
    });
}
// ---------------------------------------------------------------------------
// Historial de undo/redo (almacenado en <proyecto>/Memory/undo_history.json)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// scene.undo
// ---------------------------------------------------------------------------
ResultadoComando escenaUndo(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    std::string ruta = rutaHistorialUndo(nombreProyecto);
    nlohmann::json historial;
    if (!cargarHistorial(ruta, historial) || historial.empty()) {
        return ResultadoComando::exito({
            {"project", nombreProyecto},
            {"undone", false},
            {"message", "No hay operaciones para deshacer"}
        });
    }

    nlohmann::json operacion = historial.back();
    historial.erase(historial.end() - 1);

    std::string tipo = operacion["type"].get<std::string>();
    int idObjeto = operacion["id"].get<int>();
    const nlohmann::json& antes = operacion["before"];

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();
    GameObject* obj = findObjectById(registro, idObjeto);

    bool exito = false;
    if (tipo == "settag" && obj) {
        obj->setTag(antes.value("tag", std::string("Untagged")));
        exito = true;
    } else if (tipo == "setactive" && obj) {
        obj->setState(antes.value("active", true));
        exito = true;
    } else if (tipo == "setparent" && obj) {
        int padreId = antes.value("parentId", 0);
        GameObject* padre = padreId != 0 ? findObjectById(registro, padreId) : registro->getRoot();
        if (padre) { registro->reparent(obj, padre); exito = true; }
    } else if (tipo == "unparent" && obj) {
        int padreId = antes.value("parentId", 0);
        GameObject* padre = padreId != 0 ? findObjectById(registro, padreId) : registro->getRoot();
        if (padre) { registro->reparent(obj, padre); exito = true; }
    } else if (tipo == "create" || tipo == "clone") {
        if (obj) { registro->deleteObjectByID(idObjeto); exito = true; }
    }

    // Mover al historial de redo
    std::string rutaRedo = ruta + ".redo";
    nlohmann::json historialRedo;
    cargarHistorial(rutaRedo, historialRedo);
    historialRedo.push_back(operacion);
    if (historialRedo.size() > 100)
        while (historialRedo.size() > 100) { historialRedo.erase(historialRedo.begin()); };
    guardarHistorial(rutaRedo, historialRedo);
    guardarHistorial(ruta, historial);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"undone", exito},
        {"type", tipo},
        {"id", idObjeto},
        {"message", exito ? "Operación deshecha" : "No se pudo deshacer"}
    });
}
// ---------------------------------------------------------------------------
// scene.redo
// ---------------------------------------------------------------------------
ResultadoComando escenaRedo(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    std::string rutaRedo = rutaHistorialUndo(nombreProyecto) + ".redo";
    nlohmann::json historial;
    if (!cargarHistorial(rutaRedo, historial) || historial.empty()) {
        return ResultadoComando::exito({
            {"project", nombreProyecto},
            {"redone", false},
            {"message", "No hay operaciones para rehacer"}
        });
    }

    nlohmann::json operacion = historial.back();
    historial.erase(historial.end() - 1);

    std::string tipo = operacion["type"].get<std::string>();
    int idObjeto = operacion["id"].get<int>();
    const nlohmann::json& despues = operacion["after"];

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();
    GameObject* obj = findObjectById(registro, idObjeto);

    bool exito = false;
    if (tipo == "settag" && obj) {
        obj->setTag(despues.value("tag", std::string("Untagged")));
        exito = true;
    } else if (tipo == "setactive" && obj) {
        obj->setState(despues.value("active", true));
        exito = true;
    } else if (tipo == "setparent" && obj) {
        int padreId = despues.value("parentId", 0);
        GameObject* padre = padreId != 0 ? findObjectById(registro, padreId) : registro->getRoot();
        if (padre) { registro->reparent(obj, padre); exito = true; }
    } else if (tipo == "unparent" && obj) {
        GameObject* raiz = registro->getRoot();
        if (raiz) { registro->reparent(obj, raiz); exito = true; }
    } else if (tipo == "create" || tipo == "clone") {
        // No se puede rehacer fácilmente (perdería el nuevo id)
    }

    // Mover al historial de undo
    std::string rutaUndo = rutaHistorialUndo(nombreProyecto);
    nlohmann::json historialUndo;
    cargarHistorial(rutaUndo, historialUndo);
    historialUndo.push_back(operacion);
    if (historialUndo.size() > 100)
        while (historialUndo.size() > 100) { historialUndo.erase(historialUndo.begin()); };
    guardarHistorial(rutaUndo, historialUndo);
    guardarHistorial(rutaRedo, historial);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"redone", exito},
        {"type", tipo},
        {"id", idObjeto},
        {"message", exito ? "Operación rehecha" : "No se pudo rehacer"}
    });
}
// ---------------------------------------------------------------------------
// scene.autosave
// ---------------------------------------------------------------------------
ResultadoComando escenaAutoSave(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    std::string dirBinarios = ProjectPaths::directorioBinarios(nombreProyecto);
    std::error_code ec;
    std::filesystem::create_directories(dirBinarios, ec);

    ListaDE<GameObject*>* lista = registro->getGameObjects();
    int guardados = 0;
    int total = 0;
    if (!lista->isEmpty()) {
        Position<GameObject*>* pos = lista->first();
        while (pos != nullptr) {
            GameObject* obj = pos->getElement();
            if (obj && obj->isDirty()) {
                obj->saveEntity(dirBinarios);
                obj->setDirty(false);
                guardados++;
            }
            total++;
            pos = (pos != lista->last()) ? lista->next(pos) : nullptr;
        }
    }

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"saved", guardados},
        {"total", total},
        {"autosaved", true}
    });
}
// ---------------------------------------------------------------------------
// scene.object.setfield
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoSetField(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"id", true, nlohmann::json::value_t::number_unsigned},
        {"field", true, nlohmann::json::value_t::string},
        {"value", true, nlohmann::json::value_t::object}, // value debe ser un objeto con el valor
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    int idObjeto = static_cast<int>(argumentos["id"].get<unsigned>());
    std::string nombreCampo = argumentos["field"].get<std::string>();
    const nlohmann::json& valorJson = argumentos["value"];

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    GameObject* obj = findObjectById(registro, idObjeto);
    if (!obj) {
        registrarErrorUso("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    Script* script = obj->getComponent<Script>();
    if (!script) {
        registrarErrorUso("component_not_found", "El objeto no tiene componente Script");
        return ResultadoComando::fallo("component_not_found",
                                      "El objeto no tiene componente Script",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    const auto& campos = script->obtenerCampos();
    int indiceCampo = -1;
    for (size_t i = 0; i < campos.size(); ++i) {
        if (campos[i].nombre == nombreCampo) {
            indiceCampo = static_cast<int>(i);
            break;
        }
    }

    if (indiceCampo < 0) {
        registrarErrorUso("field_not_found",
                         "Campo '" + nombreCampo + "' no encontrado en el script");
        return ResultadoComando::fallo("field_not_found",
                                      "Campo '" + nombreCampo + "' no encontrado",
                                      codigosSalida::kRecursoNoEncontrado);
    }

    const auto& def = campos[indiceCampo];
    ReflejoScripts::ValorCampo valor;
    valor.nombre = nombreCampo;
    valor.tag = def.tag;

    bool conversionOk = true;
    switch (def.tag) {
        case ReflejoScripts::TagTipo::Entero:
            valor.contenido = valorJson.value("v", 0);
            break;
        case ReflejoScripts::TagTipo::Flotante:
            valor.contenido = static_cast<float>(valorJson.value("v", 0.0));
            break;
        case ReflejoScripts::TagTipo::Doble:
            valor.contenido = valorJson.value("v", 0.0);
            break;
        case ReflejoScripts::TagTipo::Booleano:
            valor.contenido = valorJson.value("v", false);
            break;
        case ReflejoScripts::TagTipo::Texto:
            valor.contenido = valorJson.value("v", std::string(""));
            break;
        case ReflejoScripts::TagTipo::Vec3: {
            if (valorJson.contains("v") && valorJson["v"].is_array() &&
                valorJson["v"].size() >= 3) {
                const auto& arr = valorJson["v"];
                vec3 v{
                    static_cast<float>(arr[0].get<double>()),
                    static_cast<float>(arr[1].get<double>()),
                    static_cast<float>(arr[2].get<double>())
                };
                valor.contenido = v;
            } else {
                conversionOk = false;
            }
            break;
        }
        default:
            conversionOk = false;
            break;
    }

    if (!conversionOk) {
        registrarErrorUso("invalid_field_value",
                         "No se pudo convertir el valor para el campo '" + nombreCampo + "'");
        return ResultadoComando::fallo("invalid_field_value",
                                      "Valor invalido para el campo",
                                      codigosSalida::kErrorUso);
    }

    script->escribirCampo(indiceCampo, valor);
    obj->setDirty(true);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", idObjeto},
        {"field", nombreCampo},
        {"value", valorJson},
        {"set", true}
    });
}
}  // namespace funshi
