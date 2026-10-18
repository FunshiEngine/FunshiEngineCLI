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
#include "Nucleo/CatalogoComandos.h"

#include <unordered_map>

namespace funshi {

namespace {

// Tabla única de texto -> comando. Cada entrada agrega su caso al enum y su
// entrada en todosLosComandos(); el compilador avisa si falta alguno.
const std::unordered_map<std::string, ComandoCLI>& mapaComandos() {
    static const std::unordered_map<std::string, ComandoCLI> mapa = {
        {"help", ComandoCLI::Ayuda},
        {"version", ComandoCLI::Version},
        {"project.create", ComandoCLI::ProyectoCrear},
        {"project.list", ComandoCLI::ProyectoListar},
        {"project.config.get", ComandoCLI::ProyectoConfigLeer},
        {"project.src.create", ComandoCLI::ProyectoSrcCrear},
        {"project.dir.import", ComandoCLI::ProyectoDirImportar},
        {"project.fs.create", ComandoCLI::ProyectoFsCrear},
        {"file.create", ComandoCLI::ArchivoCrear},
        {"scene.list", ComandoCLI::EscenaListar},
        {"scene.objects", ComandoCLI::EscenaObjetos},
        {"scene.object.get", ComandoCLI::EscenaObjetoObtener},
        {"scene.hierarchy", ComandoCLI::EscenaJerarquia},
        {"asset.import", ComandoCLI::AssetImportar},
        {"script.list", ComandoCLI::ScriptListar},
        {"script.compile", ComandoCLI::ScriptCompilar},
        {"diagnostics.list", ComandoCLI::DiagnosticoListar},
        {"diagnostics.clear", ComandoCLI::DiagnosticoLimpiar},
    };
    return mapa;
}

}  // namespace

ComandoCLI comandoDesdeTexto(const std::string& texto) {
    const auto& mapa = mapaComandos();
    const auto encontrado = mapa.find(texto);
    return encontrado != mapa.end() ? encontrado->second : ComandoCLI::Desconocido;
}

std::string textoDesdeComando(ComandoCLI comando) {
    switch (comando) {
        case ComandoCLI::Ayuda: return "help";
        case ComandoCLI::Version: return "version";
        case ComandoCLI::ProyectoCrear: return "project.create";
        case ComandoCLI::ProyectoListar: return "project.list";
        case ComandoCLI::ProyectoConfigLeer: return "project.config.get";
        case ComandoCLI::ProyectoSrcCrear: return "project.src.create";
        case ComandoCLI::ProyectoDirImportar: return "project.dir.import";
        case ComandoCLI::ProyectoFsCrear: return "project.fs.create";
        case ComandoCLI::ArchivoCrear: return "file.create";
        case ComandoCLI::EscenaListar: return "scene.list";
        case ComandoCLI::EscenaObjetos: return "scene.objects";
        case ComandoCLI::EscenaObjetoObtener: return "scene.object.get";
        case ComandoCLI::EscenaJerarquia: return "scene.hierarchy";
        case ComandoCLI::AssetImportar: return "asset.import";
        case ComandoCLI::ScriptListar: return "script.list";
        case ComandoCLI::ScriptCompilar: return "script.compile";
        case ComandoCLI::DiagnosticoListar: return "diagnostics.list";
        case ComandoCLI::DiagnosticoLimpiar: return "diagnostics.clear";
        case ComandoCLI::Desconocido: return std::string();
    }
    return std::string();
}

const std::vector<ComandoCLI>& todosLosComandos() {
    static const std::vector<ComandoCLI> lista = {
        ComandoCLI::Ayuda,
        ComandoCLI::Version,
        ComandoCLI::ProyectoCrear,
        ComandoCLI::ProyectoListar,
        ComandoCLI::ProyectoConfigLeer,
        ComandoCLI::ProyectoSrcCrear,
        ComandoCLI::ProyectoDirImportar,
        ComandoCLI::ProyectoFsCrear,
        ComandoCLI::ArchivoCrear,
        ComandoCLI::EscenaListar,
        ComandoCLI::EscenaObjetos,
        ComandoCLI::EscenaObjetoObtener,
        ComandoCLI::EscenaJerarquia,
        ComandoCLI::AssetImportar,
        ComandoCLI::ScriptListar,
        ComandoCLI::ScriptCompilar,
        ComandoCLI::DiagnosticoListar,
        ComandoCLI::DiagnosticoLimpiar,
    };
    return lista;
}

}  // namespace funshi
