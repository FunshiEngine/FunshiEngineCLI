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
// Creación de archivos y carpetas en src/ y en el vfs fs/ del proyecto.
#include "Comandos/Archivos/ComandosArchivos.h"

#include <Configuracion/ProjectManager.h>
#include <Configuracion/ProjectPaths.h>

#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <filesystem>
#include <fstream>

#include "Comandos/ComandosComun.h"
namespace funshi {

namespace fs = std::filesystem;


namespace {

ResultadoComando escribirArchivoSrc(const std::string& nombreProyecto,
                                    const std::string& rutaRelativa,
                                    const std::string& contenido,
                                    bool crearPadres,
                                    bool sobrescribir,
                                    const char* etiquetaLog) {
    const std::string srcDir = ProjectPaths::directorioSrc(nombreProyecto);
    std::error_code ec;
    const fs::path rutaCompleta = fs::path(srcDir) / rutaRelativa;

    ResultadoComando errorRuta;
    if (!rutaSegura(srcDir, rutaCompleta,
                    "La ruta intenta escapar del directorio src del proyecto",
                    errorRuta)) {
        return errorRuta;
    }

    if (fs::exists(rutaCompleta, ec) && fs::is_directory(rutaCompleta, ec)) {
        const std::string mensaje = "La ruta es un directorio: " + rutaRelativa;
        registrarErrorUso("invalid_request", mensaje);
        return ResultadoComando::fallo("invalid_request", mensaje, codigosSalida::kErrorUso);
    }
    if (!sobrescribir && fs::exists(rutaCompleta, ec)) {
        const std::string mensaje = "El archivo ya existe: " + rutaRelativa + " (usa project.src.write para sobrescribir)";
        registrarErrorUso("already_exists", mensaje);
        return ResultadoComando::fallo("already_exists", mensaje, codigosSalida::kErrorUso);
    }

    if (crearPadres) {
        fs::create_directories(rutaCompleta.parent_path(), ec);
        if (ec) {
            const std::string mensaje = "No se pudieron crear directorios padre: " + ec.message();
            registrarErrorOperacional("create_failed", mensaje);
            return ResultadoComando::fallo("create_failed", mensaje, codigosSalida::kFalloEscritura);
        }
    } else if (!fs::exists(rutaCompleta.parent_path(), ec)) {
        const std::string mensaje = "El directorio padre no existe: " + rutaCompleta.parent_path().string();
        registrarErrorOperacional("not_found", mensaje);
        return ResultadoComando::fallo("not_found", mensaje, codigosSalida::kRecursoNoEncontrado);
    }

    std::ofstream archivo(rutaCompleta, std::ios::binary | std::ios::trunc);
    if (!archivo) {
        registrarErrorOperacional("create_failed", "No se pudo crear el archivo: " + rutaCompleta.string());
        return ResultadoComando::fallo("create_failed", "No se pudo crear el archivo",
                                       codigosSalida::kFalloEscritura);
    }
    archivo << contenido;
    archivo.close();

    registrarErrorOperacional(etiquetaLog, std::string(etiquetaLog) + ": " + rutaRelativa);
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"path", rutaRelativa},
        {"type", "file"},
        {"written", true},
        {"bytesWritten", static_cast<long long>(contenido.size())}
    });
}

}  // namespace

ResultadoComando proyectoSrcCrear(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    std::string error;
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
        {"path", true, nlohmann::json::value_t::string},
        {"file", false, nlohmann::json::value_t::boolean},
        {"content", false, nlohmann::json::value_t::string},
    };
    if (!validarArgumentos(argumentos, requisitos, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    nombreProyecto = argumentos["project"].get<std::string>();
    const std::string rutaRelativa = argumentos["path"].get<std::string>();
    const bool esArchivo = argumentos.value("file", false);
    const std::string contenido = argumentos.value("content", std::string{});

    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        return ResultadoComando::fallo("invalid_project", error,
                                       codigosSalida::kProyectoInvalido);
    }

    if (esArchivo) {
        auto r = escribirArchivoSrc(nombreProyecto, rutaRelativa, contenido, true, false, "file_created");
        if (r.respuesta.ok) r.respuesta.resultado["created"] = true;
        return r;
    }

    const std::string srcDir = ProjectPaths::directorioSrc(nombreProyecto);
    std::error_code ec;
    const fs::path rutaCompleta = fs::path(srcDir) / rutaRelativa;

    ResultadoComando errorRuta;
    if (!rutaSegura(srcDir, rutaCompleta,
                    "La ruta intenta escapar del directorio src del proyecto",
                    errorRuta)) {
        return errorRuta;
    }

    const bool creado = fs::create_directories(rutaCompleta, ec);
    if (ec) {
        const std::string mensaje = "No se pudo crear el directorio: " + ec.message();
        registrarErrorOperacional("create_failed", mensaje);
        return ResultadoComando::fallo("create_failed", mensaje,
                                       codigosSalida::kFalloEscritura);
    }
    registrarErrorOperacional("dir_created", "Directorio creado: " + rutaRelativa);
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"path", rutaRelativa},
        {"type", "directory"},
        {"created", creado}
    });
}

ResultadoComando proyectoSrcEscribir(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    std::string error;
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
        {"path", true, nlohmann::json::value_t::string},
        {"content", true, nlohmann::json::value_t::string},
    };
    if (!validarArgumentos(argumentos, requisitos, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    nombreProyecto = argumentos["project"].get<std::string>();
    const std::string rutaRelativa = argumentos["path"].get<std::string>();
    const std::string contenido = argumentos["content"].get<std::string>();

    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        return ResultadoComando::fallo("invalid_project", error,
                                       codigosSalida::kProyectoInvalido);
    }

    return escribirArchivoSrc(nombreProyecto, rutaRelativa, contenido, true, true, "file_written");
}


ResultadoComando proyectoFsCrear(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    std::string error;
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
        {"name", true, nlohmann::json::value_t::string},
    };
    if (!validarArgumentos(argumentos, requisitos, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    nombreProyecto = argumentos["project"].get<std::string>();
    const std::string nombre = argumentos["name"].get<std::string>();

    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        return ResultadoComando::fallo("invalid_project", error,
                                       codigosSalida::kProyectoInvalido);
    }

    std::error_code ec;
    const fs::path raizFs = fs::path(ProjectPaths::directorioProyecto(nombreProyecto)) / "fs";
    const fs::path destino = raizFs / nombre;

    // No permitir que la ruta escape de <proyecto>/fs.
    ResultadoComando errorRuta;
    if (!rutaSegura(raizFs, destino,
                    "La ruta intenta escapar del directorio fs del proyecto",
                    errorRuta)) {
        return errorRuta;
    }

    fs::create_directories(destino, ec);
    if (ec) {
        const std::string mensaje = "No se pudo crear el directorio: " + ec.message();
        registrarErrorOperacional("create_failed", mensaje);
        return ResultadoComando::fallo("create_failed", mensaje,
                                       codigosSalida::kFalloEscritura);
    }

    registrarErrorOperacional("fs_dir_created", "Directorio fs creado: " + nombre);
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"name", nombre},
        {"type", "directory"},
        {"created", true}
    });
}

ResultadoComando archivoCrear(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string nombreProyecto;
    std::string error;
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
        {"name", true, nlohmann::json::value_t::string},
        {"path", false, nlohmann::json::value_t::string},
        {"content", false, nlohmann::json::value_t::string},
    };
    if (!validarArgumentos(argumentos, requisitos, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    nombreProyecto = argumentos["project"].get<std::string>();
    const std::string nombre = argumentos["name"].get<std::string>();
    const std::string subruta = argumentos.value("path", std::string{});
    const std::string contenido = argumentos.value("content", std::string{});

    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        return ResultadoComando::fallo("invalid_project", error,
                                       codigosSalida::kProyectoInvalido);
    }

    std::error_code ec;
    const fs::path raizFs = fs::path(ProjectPaths::directorioProyecto(nombreProyecto)) / "fs";
    const fs::path dirDestino = subruta.empty() ? raizFs : (raizFs / subruta);
    const fs::path destino = dirDestino / nombre;

    // No permitir que la ruta escape de <proyecto>/fs.
    ResultadoComando errorRuta;
    if (!rutaSegura(raizFs, destino,
                    "La ruta intenta escapar del directorio fs del proyecto",
                    errorRuta)) {
        return errorRuta;
    }

    fs::create_directories(dirDestino, ec);
    if (ec) {
        const std::string mensaje = "No se pudo crear el directorio padre: " + ec.message();
        registrarErrorOperacional("create_failed", mensaje);
        return ResultadoComando::fallo("create_failed", mensaje,
                                       codigosSalida::kFalloEscritura);
    }

    std::ofstream archivo(destino, std::ios::binary | std::ios::trunc);
    if (!archivo) {
        registrarErrorOperacional("create_failed", "No se pudo crear el archivo: " + nombre);
        return ResultadoComando::fallo("create_failed", "No se pudo crear el archivo",
                                       codigosSalida::kFalloEscritura);
    }
    archivo << contenido;
    archivo.close();

    registrarErrorOperacional("file_created", "Archivo creado en fs: " + nombre);
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"name", nombre},
        {"path", subruta},
        {"type", "file"},
        {"created", true}
    });
}
}  // namespace funshi
