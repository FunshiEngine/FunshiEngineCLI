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
                    m->setPath(argumentos["value"].get<std::string>());
                    modificado = true;
                }
            }
        }
    } else if (componentType == "Script") {
        if (auto* s = obj->getComponent<Script>()) {
            if (field == "scriptPath") {
                if (argumentos.contains("value") && argumentos["value"].is_string()) {
                    s->setDllPath(argumentos["value"].get<std::string>());
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
