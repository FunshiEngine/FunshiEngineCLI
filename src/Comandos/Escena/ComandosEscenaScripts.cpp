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
// Scripts de la escena: compilar, recargar y campos expuestos.
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
// scene.script.compile
// ---------------------------------------------------------------------------
ResultadoComando escenaScriptCompilar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
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
            obj->setDirty(true);
            guardarEscena(contexto, nombreProyecto);
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
ResultadoComando escenaScriptRecargar(const nlohmann::json& argumentosIn) {
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

    // Carga/recompila aunque no estuviera cargado (p. ej. tras addcomponent en CLI).
    script->recargar(obj);
    obj->setDirty(true);
    guardarEscena(contexto, nombreProyecto);

    const bool cargado = script->estaCargado();
    nlohmann::json resp = {
        {"project", nombreProyecto},
        {"id", idObjeto},
        {"scriptPath", script->getPath()},
        {"reloaded", cargado},
        {"loaded", cargado},
        {"message", cargado ? "Script recargado correctamente" : "El script no se pudo cargar"},
    };
    if (!cargado) {
        resp["error"] = script->ultimoError();
        resp["fields"] = nlohmann::json::array();
        return ResultadoComando::exito(resp);
    }
    nlohmann::json campos = nlohmann::json::array();
    for (const auto& c : script->obtenerCampos()) campos.push_back(c.nombre);
    resp["fields"] = std::move(campos);
    return ResultadoComando::exito(resp);
}
// ---------------------------------------------------------------------------
// scene.object.setfield
// ---------------------------------------------------------------------------
ResultadoComando escenaObjetoSetField(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
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

    // Cargar si aún no se intentó (p. ej. script agregado por CLI sin ciclo de play).
    if (!script->estaCargado() && !script->rutaFuente().empty()) {
        script->cargarSiNecesario();
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
        nlohmann::json disponibles = nlohmann::json::array();
        for (const auto& c : campos) disponibles.push_back(c.nombre);
        std::string mensaje = "Campo '" + nombreCampo + "' no encontrado en el script.";
        mensaje += " Campos disponibles: " + disponibles.dump() + ".";
        if (campos.empty()) {
            mensaje += " El script no expone campos serializables; verifica que esté compilado/cargado, que los campos sean públicos, y recarga con scene.script.reload o re-agrega el componente Script.";
            if (!script->estaCargado()) mensaje += " ultimoError=" + script->ultimoError();
        }
        registrarErrorUso("field_not_found", mensaje);
        return ResultadoComando::fallo("field_not_found", mensaje,
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
    guardarEscena(contexto, nombreProyecto);

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"id", idObjeto},
        {"field", nombreCampo},
        {"value", valorJson},
        {"set", true}
    });
}

}  // namespace funshi
