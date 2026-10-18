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
#include "Comandos/ComandosArchivos.h"

#include <Configuracion/ProjectManager.h>
#include <Configuracion/ProjectPaths.h>

#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <filesystem>
#include <fstream>

namespace funshi {

namespace fs = std::filesystem;

namespace {

// Resuelve args.project y el proyecto. Si falla, deja en `resultado` la
// respuesta de error y devuelve false.
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

// Verifica que `destino` quede bajo `raiz` tras resolver rutas canónicas.
// Devuelve false con `resultado` en error si la ruta escapa.
bool rutaSegura(const fs::path& raiz, const fs::path& destino,
                const std::string& mensaje, ResultadoComando& resultado) {
    std::error_code ec;
    const fs::path raizAbsoluta = fs::weakly_canonical(raiz, ec);
    const fs::path destinoAbsoluto = fs::weakly_canonical(destino, ec);
    if (ec || destinoAbsoluto.string().find(raizAbsoluta.string()) != 0) {
        registrarErrorOperacional("invalid_path", mensaje);
        resultado = ResultadoComando::fallo("invalid_path", mensaje,
                                            codigosSalida::kRecursoNoEncontrado);
        return false;
    }
    return true;
}

}  // namespace

ResultadoComando proyectoSrcCrear(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    std::string error;
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
        {"path", true, nlohmann::json::value_t::string},
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

    const std::string srcDir = ProjectPaths::directorioSrc(nombreProyecto);
    std::error_code ec;
    const fs::path rutaCompleta = fs::path(srcDir) / rutaRelativa;

    // Validar que la ruta no escape del directorio src.
    const fs::path srcAbsoluto = fs::weakly_canonical(srcDir, ec);
    const fs::path destinoAbsoluto = fs::weakly_canonical(rutaCompleta.parent_path(), ec);
    if (ec || destinoAbsoluto.string().find(srcAbsoluto.string()) != 0) {
        const std::string mensaje = "La ruta intenta escapar del directorio src del proyecto";
        registrarErrorOperacional("invalid_path", mensaje);
        return ResultadoComando::fallo("invalid_path", mensaje,
                                       codigosSalida::kRecursoNoEncontrado);
    }

    if (esArchivo) {
        // Crear directorios padre si no existen.
        fs::create_directories(rutaCompleta.parent_path(), ec);
        if (ec) {
            const std::string mensaje =
                "No se pudieron crear directorios padre: " + ec.message();
            registrarErrorOperacional("create_failed", mensaje);
            return ResultadoComando::fallo("create_failed", mensaje,
                                           codigosSalida::kFalloEscritura);
        }
        // Crear archivo.
        std::ofstream archivo(rutaCompleta, std::ios::binary | std::ios::trunc);
        if (!archivo) {
            registrarErrorOperacional("create_failed",
                                      "No se pudo crear el archivo: " + rutaCompleta.string());
            return ResultadoComando::fallo("create_failed", "No se pudo crear el archivo",
                                           codigosSalida::kFalloEscritura);
        }
        archivo << contenido;
        archivo.close();
        registrarErrorOperacional("file_created", "Archivo creado: " + rutaRelativa);
        return ResultadoComando::exito({
            {"project", nombreProyecto},
            {"path", rutaRelativa},
            {"type", "file"},
            {"created", true}
        });
    }
    // Crear directorio.
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

ResultadoComando proyectoDirImportar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    std::string error;
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
        {"source", true, nlohmann::json::value_t::string},
        {"path", true, nlohmann::json::value_t::string},
    };
    if (!validarArgumentos(argumentos, requisitos, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    nombreProyecto = argumentos["project"].get<std::string>();
    const std::string sourcePath = argumentos["source"].get<std::string>();
    const std::string rutaRelativa = argumentos["path"].get<std::string>();
    const bool overwrite = argumentos.value("overwrite", false);

    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        return ResultadoComando::fallo("invalid_project", error,
                                       codigosSalida::kProyectoInvalido);
    }

    std::error_code ec;
    if (!fs::exists(sourcePath, ec) || ec) {
        registrarErrorOperacional("source_not_found", "La carpeta origen no existe: " + sourcePath);
        return ResultadoComando::fallo("source_not_found",
                                       "La carpeta origen no existe: " + sourcePath,
                                       codigosSalida::kRecursoNoEncontrado);
    }
    if (!fs::is_directory(sourcePath, ec) || ec) {
        registrarErrorOperacional("source_not_directory", "El origen no es un directorio: " + sourcePath);
        return ResultadoComando::fallo("source_not_directory",
                                       "El origen no es un directorio: " + sourcePath,
                                       codigosSalida::kRecursoNoEncontrado);
    }

    const std::string srcDir = ProjectPaths::directorioSrc(nombreProyecto);
    const fs::path destPath = fs::path(srcDir) / rutaRelativa;

    // Validar que la ruta no escape del directorio src.
    ResultadoComando errorRuta;
    if (!rutaSegura(srcDir, destPath,
                    "La ruta destino intenta escapar del directorio src del proyecto",
                    errorRuta)) {
        return errorRuta;
    }

    // Si el destino existe y no se permite overwrite, error.
    if (fs::exists(destPath, ec) && !overwrite) {
        const std::string mensaje =
            "El destino ya existe: " + rutaRelativa + " (usa overwrite: true para reemplazar)";
        registrarErrorOperacional("destination_exists", mensaje);
        return ResultadoComando::fallo("destination_exists", mensaje,
                                       codigosSalida::kRecursoNoEncontrado);
    }

    // Si existe y overwrite=true, borrar primero.
    if (fs::exists(destPath, ec) && overwrite) {
        fs::remove_all(destPath, ec);
        if (ec) {
            const std::string mensaje =
                "No se pudo eliminar el destino existente: " + ec.message();
            registrarErrorOperacional("remove_failed", mensaje);
            return ResultadoComando::fallo("remove_failed", mensaje,
                                           codigosSalida::kFalloEscritura);
        }
    }

    // Copiar recursivamente.
    const fs::copy_options opciones = fs::copy_options::recursive |
                                      fs::copy_options::overwrite_existing;
    fs::copy(sourcePath, destPath, opciones, ec);
    if (ec) {
        const std::string mensaje = "Error al copiar la carpeta: " + ec.message();
        registrarErrorOperacional("copy_failed", mensaje);
        return ResultadoComando::fallo("copy_failed", mensaje,
                                       codigosSalida::kFalloEscritura);
    }

    // Contar archivos copiados.
    size_t fileCount = 0;
    for (const auto& entry : fs::recursive_directory_iterator(destPath, ec)) {
        if (entry.is_regular_file(ec)) fileCount++;
    }

    registrarErrorOperacional("dir_imported",
                              "Carpeta importada: " + rutaRelativa + " (" +
                                  std::to_string(fileCount) + " archivos)");
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"source", sourcePath},
        {"destination", rutaRelativa},
        {"filesCopied", static_cast<long long>(fileCount)},
        {"overwrite", overwrite}
    });
}

ResultadoComando proyectoFsCrear(const nlohmann::json& argumentos) {
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

ResultadoComando archivoCrear(const nlohmann::json& argumentos) {
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
