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
#include "Comandos/ComandosProyecto.h"

#include <Configuracion/ConfigPersistence.h>
#include <Configuracion/DimensionProyecto.h>
#include <Configuracion/ProjectManager.h>
#include <Configuracion/ProjectPaths.h>

#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <system_error>

namespace funshi {

ResultadoComando proyectoCrear(const nlohmann::json& argumentos) {
    if (argumentos.contains("name") && !argumentos["name"].is_string()) {
        registrarErrorUso("invalid_request", "args.name debe ser un string");
        return ResultadoComando::fallo("invalid_request", "args.name debe ser un string",
                                       codigosSalida::kErrorUso);
    }
    const std::string nombre = argumentos.value("name", std::string{});
    DimensionProyecto dimension = DimensionProyecto::TresD;
    if (!dimensionDesdeArgs(argumentos, dimension)) {
        const std::string mensaje =
            "args.dimension invalido: use 2/3 o \"2d\"/\"3d\"/\"DosD\"/\"TresD\"";
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje,
                                       codigosSalida::kErrorUso);
    }

    std::error_code error;
    if (!ProjectManager::crearProyectoNuevo(nombre, error)) {
        const std::string codigo = error == std::errc::file_exists
                                       ? "project_exists"
                                       : error == std::errc::invalid_argument
                                             ? "invalid_project_name"
                                             : "project_create_failed";
        const std::string mensaje =
            codigo == "project_exists"
                ? "Ya existe un proyecto con ese nombre"
                : codigo == "invalid_project_name"
                      ? "El nombre del proyecto no es valido"
                      : error.message();
        registrarErrorOperacional(codigo, mensaje);
        return ResultadoComando::fallo(codigo, mensaje, codigosSalida::kFalloGeneral);
    }

    registrarEvento("Proyecto creado", nombre);

    // Persistir la dimensión elegida. crearProyectoNuevo solo crea la
    // estructura; la dimensión se fija por el único camino de escritura
    // (guardarProyecto), igual que la GUI, para que project.config.get y
    // project.list la lean y no caigan al default 3D.
    ConfigPersistence::Proyecto config = ConfigPersistence::cargarProyecto(nombre);
    config.dimension = dimension;
    ConfigPersistence::guardarProyecto(config, nombre);

    return ResultadoComando::exito(
        {{"name", nombre},
         {"path", ProjectPaths::directorioProyecto(nombre)},
         {"dimension", dimensionProyectoAEntero(dimension)}});
}

ResultadoComando proyectoListar(const nlohmann::json&) {
    const auto proyectos = ProjectManager::descubrirProyectos();
    nlohmann::json resultado = nlohmann::json::array();
    for (const auto& p : proyectos) {
        ConfigPersistence::Proyecto config = ConfigPersistence::cargarProyecto(p.nombre);
        resultado.push_back({
            {"name", p.nombre},
            {"path", p.ruta},
            {"srcPath", p.rutaSrc},
            {"valid", p.valido},
            {"dimension", dimensionProyectoAEntero(config.dimension)}
        });
    }
    return ResultadoComando::exito(
        {{"projects", std::move(resultado)}, {"count", proyectos.size()}});
}

ResultadoComando proyectoConfigLeer(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    std::string error;
    if (!argumentoProyecto(argumentos, nombreProyecto, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        return ResultadoComando::fallo("invalid_project", error,
                                       codigosSalida::kProyectoInvalido);
    }
    ConfigPersistence::Proyecto config = ConfigPersistence::cargarProyecto(nombreProyecto);
    nlohmann::json resultado = {
        {"name", nombreProyecto},
        {"dimension", dimensionProyectoAEntero(config.dimension)},
        {"version", config.version},
        {"ventanaCamarasAbierta", config.ventanaCamarasAbierta},
        {"gizmoOperacion", config.gizmoOperacion},
        {"gizmoGlobal", config.gizmoGlobal},
        {"camaraActivaId", config.camaraActivaId},
        {"estadoVentanas", config.estadoVentanas}
    };
    return ResultadoComando::exito(std::move(resultado));
}

}  // namespace funshi
