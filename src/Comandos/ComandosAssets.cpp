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
#include "Comandos/ComandosAssets.h"

#include <Assets/AssetPath.h>
#include <Configuracion/ProjectManager.h>
#include <Configuracion/ProjectPaths.h>

#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

#include <algorithm>
#include <filesystem>

namespace funshi {

namespace fs = std::filesystem;

ResultadoComando assetImportar(const nlohmann::json& argumentos) {
    std::string nombreProyecto;
    std::string error;
    const std::vector<RequisitoArg> requisitos = {
        {"project", true, nlohmann::json::value_t::string},
        {"source", true, nlohmann::json::value_t::string},
    };
    if (!validarArgumentos(argumentos, requisitos, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    nombreProyecto = argumentos["project"].get<std::string>();
    const std::string fuente = argumentos["source"].get<std::string>();

    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        return ResultadoComando::fallo("invalid_project", error,
                                       codigosSalida::kProyectoInvalido);
    }

    std::error_code ec;
    const fs::path origen(fuente);
    if (!fs::exists(origen, ec) || ec) {
        registrarErrorOperacional("source_not_found", "El archivo origen no existe: " + fuente);
        return ResultadoComando::fallo("source_not_found", "El archivo origen no existe",
                                       codigosSalida::kRecursoNoEncontrado);
    }
    if (!fs::is_regular_file(origen, ec) || ec) {
        registrarErrorOperacional("source_not_file", "El origen no es un archivo regular");
        return ResultadoComando::fallo("source_not_file", "El origen no es un archivo regular",
                                       codigosSalida::kRecursoNoEncontrado);
    }

    // Clasificación por extensión: modelo, textura o audio.
    const std::string ext = AssetPath::extension(fuente);
    std::string tipo;
    std::string subcarpeta;
    static const char* extModelo[] = {"obj", "fbx", "gltf", "glb", "dae", "blend"};
    static const char* extTextura[] = {"png", "jpg", "jpeg", "tga", "bmp", "hdr"};
    static const char* extAudio[] = {"wav", "ogg", "mp3", "flac"};
    if (std::find(std::begin(extModelo), std::end(extModelo), ext) != std::end(extModelo)) {
        tipo = "model";
        subcarpeta = "Modelos";
    } else if (std::find(std::begin(extTextura), std::end(extTextura), ext) != std::end(extTextura)) {
        tipo = "texture";
        subcarpeta = "Texturas";
    } else if (std::find(std::begin(extAudio), std::end(extAudio), ext) != std::end(extAudio)) {
        tipo = "audio";
        subcarpeta = "Sonidos";
    } else {
        const std::string mensaje = "Extension no soportada para importar: " + ext;
        registrarErrorUso("unsupported_extension", mensaje);
        return ResultadoComando::fallo("unsupported_extension", mensaje,
                                       codigosSalida::kErrorUso);
    }

    // El audio vive en src<proyecto>/Sonidos; modelo y textura en la raíz
    // de assets src<proyecto>/<subcarpeta>. Ambas cuelgan de directorioSrc.
    const fs::path dirDestino = fs::path(ProjectPaths::directorioSrc(nombreProyecto)) / subcarpeta;
    fs::create_directories(dirDestino, ec);
    if (ec) {
        registrarErrorOperacional("create_failed", "No se pudo crear el directorio destino: " + ec.message());
        return ResultadoComando::fallo("create_failed", "No se pudo crear el directorio destino",
                                       codigosSalida::kFalloEscritura);
    }
    const fs::path destino = dirDestino / origen.filename();
    fs::copy_file(origen, destino, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        registrarErrorOperacional("copy_failed", "No se pudo copiar el asset: " + ec.message());
        return ResultadoComando::fallo("copy_failed", "No se pudo copiar el asset",
                                       codigosSalida::kFalloEscritura);
    }

    const std::string rutaRelativa =
        AssetPath::normalize(subcarpeta + "/" + origen.filename().string());
    registrarErrorOperacional("asset_imported", "Asset importado: " + rutaRelativa);
    return ResultadoComando::exito({
        {"project", nombreProyecto},
        {"path", rutaRelativa},
        {"type", tipo},
        {"exists", true}
    });
}

// ---------------------------------------------------------------------------
// asset.validar
// ---------------------------------------------------------------------------
ResultadoComando assetValidar(const nlohmann::json& argumentos) {
    const std::vector<RequisitoArg> requisitos = {
        {"path", true, nlohmann::json::value_t::string},
    };
    std::string errorValidacion;
    if (!validarArgumentos(argumentos, requisitos, errorValidacion)) {
        registrarErrorUso("invalid_request", errorValidacion);
        return ResultadoComando::fallo("invalid_request", errorValidacion,
                                      codigosSalida::kErrorUso);
    }

    std::string path = argumentos["path"].get<std::string>();
    const std::string ext = AssetPath::extension(path);

    static const char* extModelo[] = {"obj", "fbx", "gltf", "glb", "dae", "blend"};
    static const char* extTextura[] = {"png", "jpg", "jpeg", "tga", "bmp", "hdr"};
    static const char* extAudio[] = {"wav", "ogg", "mp3", "flac"};

    std::string tipo;
    bool soportado = false;
    if (std::find(std::begin(extModelo), std::end(extModelo), ext) != std::end(extModelo)) {
        tipo = "model";
        soportado = true;
    } else if (std::find(std::begin(extTextura), std::end(extTextura), ext) != std::end(extTextura)) {
        tipo = "texture";
        soportado = true;
    } else if (std::find(std::begin(extAudio), std::end(extAudio), ext) != std::end(extAudio)) {
        tipo = "audio";
        soportado = true;
    }

    return ResultadoComando::exito({
        {"path", path},
        {"extension", ext},
        {"supported", soportado},
        {"type", tipo}
    });
}

// ---------------------------------------------------------------------------
// asset.extensions
// ---------------------------------------------------------------------------
ResultadoComando assetListarExtensiones(const nlohmann::json& argumentos) {
    nlohmann::json modelos = nlohmann::json::array();
    for (const char* ext : {"obj", "fbx", "gltf", "glb", "dae", "blend"})
        modelos.push_back(ext);

    nlohmann::json texturas = nlohmann::json::array();
    for (const char* ext : {"png", "jpg", "jpeg", "tga", "bmp", "hdr"})
        texturas.push_back(ext);

    nlohmann::json audios = nlohmann::json::array();
    for (const char* ext : {"wav", "ogg", "mp3", "flac"})
        audios.push_back(ext);

    return ResultadoComando::exito({
        {"model", modelos},
        {"texture", texturas},
        {"audio", audios}
    });
}
}  // namespace funshi
