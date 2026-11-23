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
// Utilidades compartidas por los comandos de escena (ComandosEscena*.cpp).
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
// Helper: obtiene el nombre legible del tipo de un componente usando dynamic_cast
// ---------------------------------------------------------------------------
std::string tipoComponenteNombre(Component* comp) {
    if (dynamic_cast<Transform*>(comp)) return "Transform";
    if (dynamic_cast<Script*>(comp)) return "Script";
    if (dynamic_cast<Model*>(comp)) return "Model";
    if (dynamic_cast<Color*>(comp)) return "Color";
    if (dynamic_cast<AudioSource*>(comp)) return "AudioSource";
    if (dynamic_cast<Collider*>(comp)) return "Collider";
    if (dynamic_cast<RigidBody*>(comp)) return "RigidBody";
    return "Unknown";
}

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

// Guarda la escena usando la convención del motor: el serializador recibe el
// prefijo del archivo de índice y escribe los binarios de objetos en la
// subcarpeta Scene.
void guardarEscena(ContextoEscena& contexto, const std::string& nombreProyecto) {
    contexto.serializer().save(ProjectPaths::rutaScenePrefijo(nombreProyecto));
}

}  // namespace funshi
