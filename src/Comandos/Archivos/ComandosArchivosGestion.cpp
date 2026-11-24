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
// Importación, listado, borrado y renombrado dentro de src/.
#include "Comandos/Archivos/ComandosArchivos.h"
#include "Comandos/ComandosBorrado.h"
#include "Comandos/ComandosPapelera.h"
#include "Comandos/Escena/ComandosEscenaInterno.h"
#include <Assets/AssetPath.h>

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

ResultadoComando proyectoDirImportar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    std::string error;
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
        {"source", true, nlohmann::json::value_t::string},
        {"path", true, nlohmann::json::value_t::string},
        {"overwrite", false, nlohmann::json::value_t::boolean},
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

ResultadoComando proyectoSrcListar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"path", false, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    const std::string subruta = argumentos.value("path", std::string{});
    const std::string srcDir = ProjectPaths::directorioSrc(nombreProyecto);
    std::error_code ec;
    nlohmann::json entries = nlohmann::json::array();

    std::filesystem::path dirPath = std::filesystem::path(srcDir);
    if (!subruta.empty()) {
        dirPath /= subruta;
    }

    // Validar que la ruta no escape del directorio src.
    ResultadoComando errorRuta;
    if (!rutaSegura(srcDir, dirPath,
                    "La ruta intenta escapar del directorio src del proyecto",
                    errorRuta)) {
        return errorRuta;
    }

    if (std::filesystem::exists(dirPath, ec) && !ec) {
        if (!std::filesystem::is_directory(dirPath, ec)) {
            const std::string mensaje = "La ruta no es un directorio: " + subruta;
            registrarErrorOperacional("not_a_directory", mensaje);
            return ResultadoComando::fallo("not_a_directory", mensaje,
                                           codigosSalida::kRecursoNoEncontrado);
        }
        for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
            if (!entry.is_regular_file(ec) && !entry.is_directory(ec)) continue;
            nlohmann::json item;
            item["name"] = entry.path().filename().string();
            std::error_code rec;
            auto rel = std::filesystem::relative(entry.path(), fs::path(srcDir), rec);
            item["path"] = rec ? entry.path().filename().string() : rel.generic_string();
            item["type"] = entry.is_directory(ec) ? "directory" : "file";
            if (entry.is_regular_file(ec)) {
                item["size"] = static_cast<long long>(entry.file_size(ec));
            }
            entries.push_back(std::move(item));
        }
    } else if (!subruta.empty()) {
        const std::string mensaje = "El directorio no existe: " + subruta;
        registrarErrorOperacional("not_found", mensaje);
        return ResultadoComando::fallo("not_found", mensaje,
                                       codigosSalida::kRecursoNoEncontrado);
    }

    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"entries", entries},
        {"count", static_cast<long long>(entries.size())}
    });
}

ResultadoComando proyectoSrcBorrar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"path", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion, codigosSalida::kErrorUso);
    }

    const std::string rutaRelativa = argumentos["path"].get<std::string>();
    const std::string srcDir = ProjectPaths::directorioSrc(nombreProyecto);
    std::error_code ec;
    const fs::path rutaCompleta = fs::path(srcDir) / rutaRelativa;

    // Validar que la ruta no escape del directorio src.
    ResultadoComando errorRuta;
    if (!rutaSegura(srcDir, rutaCompleta,
                    "La ruta intenta escapar del directorio src del proyecto",
                    errorRuta)) {
        return errorRuta;
    }

    if (!std::filesystem::exists(rutaCompleta, ec) || ec) {
        const std::string mensaje = "El archivo o directorio no existe: " + rutaRelativa;
        registrarErrorOperacional("not_found", mensaje);
        return ResultadoComando::fallo("not_found", mensaje, codigosSalida::kRecursoNoEncontrado);
    }

    // A la papelera; un archivo que algún objeto usa se quita de esos objetos.
    bool esDirectorio = std::filesystem::is_directory(rutaCompleta, ec);
    nlohmann::json quitadas = nlohmann::json::array();
    if (!esDirectorio) {
        ContextoEscena contexto(nombreProyecto);
        quitadas = quitarReferenciasA(&contexto.registro(), AssetPath::normalize(rutaRelativa), rutaCompleta.string());
        if (!quitadas.empty()) guardarEscena(contexto, nombreProyecto);
    }
    std::string error;
    const int enPapelera = moverAPapelera(nombreProyecto, AssetPath::normalize(rutaRelativa), rutaCompleta,
                                          esDirectorio ? "directory" : "file", quitadas, error);
    if (enPapelera < 0) {
        registrarErrorOperacional("remove_failed", error);
        return ResultadoComando::fallo("remove_failed", error, codigosSalida::kFalloEscritura);
    }

    registrarErrorOperacional("src_deleted", "Eliminado de src (a la papelera): " + rutaRelativa);
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"path", rutaRelativa},
        {"type", esDirectorio ? "directory" : "file"},
        {"deleted", true},
        {"trashId", enPapelera},
        {"referencesRemoved", quitadas}
    });
}

ResultadoComando proyectoSrcRenombrar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, nombreProyecto, resultado)) return resultado;

    const std::vector<RequisitoArg> requisitos = {
        {"path", true, nlohmann::json::value_t::string},
        {"newPath", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion, codigosSalida::kErrorUso);
    }

    const std::string rutaRelativa = argumentos["path"].get<std::string>();
    const std::string nuevaRutaRelativa = argumentos["newPath"].get<std::string>();
    const std::string srcDir = ProjectPaths::directorioSrc(nombreProyecto);
    std::error_code ec;
    const fs::path rutaOrigen = fs::path(srcDir) / rutaRelativa;
    const fs::path rutaDestino = fs::path(srcDir) / nuevaRutaRelativa;

    // Validar que ambas rutas no escapen del directorio src.
    ResultadoComando errorRuta;
    if (!rutaSegura(srcDir, rutaOrigen,
                    "La ruta intenta escapar del directorio src del proyecto",
                    errorRuta) ||
        !rutaSegura(srcDir, rutaDestino.parent_path(),
                    "La ruta intenta escapar del directorio src del proyecto",
                    errorRuta)) {
        return errorRuta;
    }

    if (!std::filesystem::exists(rutaOrigen, ec) || ec) {
        const std::string mensaje = "El archivo o directorio origen no existe: " + rutaRelativa;
        registrarErrorOperacional("not_found", mensaje);
        return ResultadoComando::fallo("not_found", mensaje, codigosSalida::kRecursoNoEncontrado);
    }

    if (std::filesystem::exists(rutaDestino, ec) && !ec) {
        const std::string mensaje = "El destino ya existe: " + nuevaRutaRelativa;
        registrarErrorOperacional("destination_exists", mensaje);
        return ResultadoComando::fallo("destination_exists", mensaje, codigosSalida::kRecursoNoEncontrado);
    }

    // Crear directorios padre del destino si no existen.
    std::error_code ec2;
    fs::create_directories(rutaDestino.parent_path(), ec2);
    if (ec2) {
        const std::string mensaje = "No se pudieron crear directorios padre: " + ec2.message();
        registrarErrorOperacional("create_failed", mensaje);
        return ResultadoComando::fallo("create_failed", mensaje, codigosSalida::kFalloEscritura);
    }

    // Intentar renombrar y capturar error
    std::error_code renameEc;
    fs::rename(rutaOrigen, rutaDestino, renameEc);
    if (renameEc) {
        registrarErrorOperacional("rename_failed", "No se pudo renombrar");
        return ResultadoComando::fallo("rename_failed", "No se pudo renombrar", codigosSalida::kFalloEscritura);
    }

    bool esDirectorio = std::filesystem::is_directory(rutaDestino, ec);
    registrarErrorOperacional("src_renamed", "Renombrado en src: " + rutaRelativa + " -> " + nuevaRutaRelativa);
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"path", rutaRelativa},
        {"newPath", nuevaRutaRelativa},
        {"type", esDirectorio ? "directory" : "file"},
        {"renamed", true}
    });
}

}  // namespace funshi
