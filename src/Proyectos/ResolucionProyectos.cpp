/*
    FunshiEngineCLI - CLI de automación del motor FunshiEngineGL
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
#include "Proyectos/ResolucionProyectos.h"

#include <Configuracion/ProjectPaths.h>

#include <filesystem>

namespace funshi {

namespace fs = std::filesystem;

bool resolverProyecto(const std::string& ruta,
                      ProjectManager::Proyecto& proyecto,
                      std::string& error) {
    if (ruta.empty()) {
        error = "La ruta del proyecto esta vacia";
        return false;
    }

    std::error_code ec;
    const fs::path raiz = fs::weakly_canonical(
        fs::absolute(ProjectPaths::directorioProyects(), ec), ec);
    if (ec) {
        error = "No se pudo resolver la ubicacion fija de proyectos";
        return false;
    }
    const fs::path recibida = fs::weakly_canonical(fs::absolute(ruta, ec), ec);
    if (ec || !fs::is_directory(recibida, ec) || ec) {
        error = "El proyecto indicado no existe o no es un directorio";
        return false;
    }

    const fs::path nombrePath = recibida.filename();
    const std::string nombre = nombrePath.string();
    if (!ProjectPaths::esNombreValido(nombre) || nombre == "." || nombre == "..") {
        error = "La ruta no identifica un proyecto valido";
        return false;
    }

    const fs::path esperado = fs::weakly_canonical(
        fs::path(ProjectPaths::directorioProyecto(nombre)), ec);
    if (ec) {
        error = "No se pudo resolver la ruta del proyecto";
        return false;
    }
    const bool mismaCarpeta = fs::equivalent(esperado, recibida, ec);
    if (ec || !mismaCarpeta) {
        error = "El proyecto debe estar dentro de la ubicacion fija del motor";
        return false;
    }
    const bool bajoRaiz = fs::equivalent(recibida.parent_path(), raiz, ec);
    if (ec || !bajoRaiz) {
        error = "El proyecto debe estar dentro de la ubicacion fija del motor";
        return false;
    }
    if (!ProjectManager::esProyectoValido(nombre)) {
        error = "El directorio no contiene la estructura de un proyecto Funshi";
        return false;
    }

    const auto encontrado = ProjectManager::buscarProyecto(nombre);
    if (!encontrado) {
        error = "No se pudo abrir el proyecto";
        return false;
    }
    proyecto = *encontrado;
    return true;
}

bool resolverProyectoPorNombre(const std::string& nombre,
                               ProjectManager::Proyecto& proyecto,
                               std::string& error) {
    if (!ProjectPaths::esNombreValido(nombre) || nombre == "." || nombre == "..") {
        error = "El nombre del proyecto no es valido";
        return false;
    }
    if (!ProjectManager::esProyectoValido(nombre)) {
        error = "El proyecto no existe o no tiene estructura valida";
        return false;
    }
    const auto encontrado = ProjectManager::buscarProyecto(nombre);
    if (!encontrado) {
        error = "No se pudo abrir el proyecto";
        return false;
    }
    proyecto = *encontrado;
    return true;
}

}  // namespace funshi
