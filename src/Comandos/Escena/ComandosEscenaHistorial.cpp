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
// Undo/redo y autoguardado de la escena.
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
#include "Comandos/ComandosBorrado.h"
#include "Comandos/ComandosPapelera.h"
namespace funshi {
// ---------------------------------------------------------------------------
// Historial de undo/redo (almacenado en <proyecto>/Memory/undo_history.json)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// scene.undo
// ---------------------------------------------------------------------------
ResultadoComando escenaUndo(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
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
    } else if (tipo == "delete") {
        // Vuelve a instanciar la copia guardada; tiene un id nuevo, que es el
        // que usará scene.redo para borrarlo otra vez.
        const int nuevo = restaurarObjetoBorrado(contexto, antes);
        if (nuevo >= 0) {
            idObjeto = nuevo;
            operacion["id"] = nuevo;
            exito = true;
            quitarDePapeleraPorPrefab(nombreProyecto, antes.value("prefab", ""));  // volvió: ya no está borrado
        }
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

    if (exito) {
        guardarEscena(contexto, nombreProyecto);
    }

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
ResultadoComando escenaRedo(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
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
    } else if (tipo == "delete" && obj) {
        exito = borrarConDescendientes(contexto, obj);
        if (exito) {
            const nlohmann::json& borrado = operacion["before"];
            anotarObjetoEnPapelera(nombreProyecto, borrado.value("name", ""), idObjeto, borrado.value("parentId", 0),
                                   borrado.value("prefab", ""));
        }
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

    if (exito) {
        guardarEscena(contexto, nombreProyecto);
    }

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
ResultadoComando escenaAutoSave(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    ContextoEscena contexto(nombreProyecto);
    SceneRegistry* registro = &contexto.registro();

    std::string dirScene = ProjectPaths::rutaSceneDir(nombreProyecto);
    std::error_code ec;
    std::filesystem::create_directories(dirScene, ec);

    ListaDE<GameObject*>* lista = registro->getGameObjects();
    int guardados = 0;
    int total = 0;
    if (!lista->isEmpty()) {
        Position<GameObject*>* pos = lista->first();
        while (pos != nullptr) {
            GameObject* obj = pos->getElement();
            if (obj && obj->isDirty()) {
                obj->saveEntity(dirScene);
                obj->setDirty(false);
                guardados++;
            }
            total++;
            pos = (pos != lista->last()) ? lista->next(pos) : nullptr;
        }
    }
    if (guardados > 0) {
        guardarEscena(contexto, nombreProyecto);
    }

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"saved", guardados},
        {"total", total},
        {"autosaved", true}
    });
}

}  // namespace funshi
