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
        {"project.src.list", ComandoCLI::ProyectoSrcListar},
        {"project.src.delete", ComandoCLI::ProyectoSrcBorrar},
        {"project.src.rename", ComandoCLI::ProyectoSrcRenombrar},
        {"project.dir.import", ComandoCLI::ProyectoDirImportar},
        {"project.fs.create", ComandoCLI::ProyectoFsCrear},
        {"file.create", ComandoCLI::ArchivoCrear},
        {"scene.list", ComandoCLI::EscenaListar},
        {"scene.objects", ComandoCLI::EscenaObjetos},
        {"scene.object.get", ComandoCLI::EscenaObjetoObtener},
        {"scene.object.getglobalpos", ComandoCLI::EscenaObjetoPosicionGlobal},
        {"scene.object.settransform", ComandoCLI::EscenaObjetoSetTransform},
        {"scene.object.setcomponent", ComandoCLI::EscenaObjetoSetComponente},
        {"scene.hierarchy", ComandoCLI::EscenaJerarquia},
        {"scene.save", ComandoCLI::EscenaGuardar},
        {"scene.object.setparent", ComandoCLI::EscenaObjetoSetPadre},
        {"scene.object.addcomponent", ComandoCLI::EscenaObjetoAddComponente},
        {"scene.object.create", ComandoCLI::EscenaObjetoCrear},
        {"scene.object.clone", ComandoCLI::EscenaObjetoClonar},
        {"scene.object.removecomponent", ComandoCLI::EscenaObjetoQuitarComponente},
        {"scene.object.unparent", ComandoCLI::EscenaObjetoDesvincularPadre},
        {"scene.object.settag", ComandoCLI::EscenaObjetoSetTag},
        {"scene.object.setactive", ComandoCLI::EscenaObjetoSetActivo},
        {"scene.object.components", ComandoCLI::EscenaObjetoComponentes},
        {"scene.object.findbytag", ComandoCLI::EscenaObjetoBuscarPorTag},
        {"scene.object.findbyname", ComandoCLI::EscenaObjetoBuscarPorNombre},
        {"scene.object.getglobaltransform", ComandoCLI::EscenaObjetoGetGlobalTransform},
        {"asset.import", ComandoCLI::AssetImportar},
        {"asset.validar", ComandoCLI::AssetValidar},
        {"asset.extensions", ComandoCLI::AssetListarExtensiones},
        {"script.list", ComandoCLI::ScriptListar},
        {"script.compile", ComandoCLI::ScriptCompilar},
        {"scene.script.compile", ComandoCLI::EscenaScriptCompilar},
        {"scene.script.reload", ComandoCLI::EscenaScriptRecargar},
        {"scene.undo", ComandoCLI::EscenaUndo},
        {"scene.redo", ComandoCLI::EscenaRedo},
        {"scene.autosave", ComandoCLI::EscenaAutoSave},
        {"scene.object.setfield", ComandoCLI::EscenaObjetoSetField},
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
        case ComandoCLI::ProyectoSrcListar: return "project.src.list";
        case ComandoCLI::ProyectoSrcBorrar: return "project.src.delete";
        case ComandoCLI::ProyectoSrcRenombrar: return "project.src.rename";
        case ComandoCLI::ProyectoDirImportar: return "project.dir.import";
        case ComandoCLI::ProyectoFsCrear: return "project.fs.create";
        case ComandoCLI::ArchivoCrear: return "file.create";
        case ComandoCLI::EscenaListar: return "scene.list";
        case ComandoCLI::EscenaObjetos: return "scene.objects";
        case ComandoCLI::EscenaObjetoObtener: return "scene.object.get";
        case ComandoCLI::EscenaObjetoPosicionGlobal: return "scene.object.getglobalpos";
        case ComandoCLI::EscenaObjetoSetTransform: return "scene.object.settransform";
        case ComandoCLI::EscenaObjetoSetComponente: return "scene.object.setcomponent";
        case ComandoCLI::EscenaJerarquia: return "scene.hierarchy";
        case ComandoCLI::EscenaGuardar: return "scene.save";
        case ComandoCLI::EscenaObjetoSetPadre: return "scene.object.setparent";
        case ComandoCLI::EscenaObjetoAddComponente: return "scene.object.addcomponent";
        case ComandoCLI::EscenaObjetoCrear: return "scene.object.create";
        case ComandoCLI::EscenaObjetoClonar: return "scene.object.clone";
        case ComandoCLI::EscenaObjetoQuitarComponente: return "scene.object.removecomponent";
        case ComandoCLI::EscenaObjetoDesvincularPadre: return "scene.object.unparent";
        case ComandoCLI::EscenaObjetoSetTag: return "scene.object.settag";
        case ComandoCLI::EscenaObjetoSetActivo: return "scene.object.setactive";
        case ComandoCLI::EscenaObjetoComponentes: return "scene.object.components";
        case ComandoCLI::EscenaObjetoBuscarPorTag: return "scene.object.findbytag";
        case ComandoCLI::EscenaObjetoBuscarPorNombre: return "scene.object.findbyname";
        case ComandoCLI::EscenaObjetoGetGlobalTransform: return "scene.object.getglobaltransform";
        case ComandoCLI::AssetImportar: return "asset.import";
        case ComandoCLI::AssetValidar: return "asset.validar";
        case ComandoCLI::AssetListarExtensiones: return "asset.extensions";
        case ComandoCLI::ScriptListar: return "script.list";
        case ComandoCLI::ScriptCompilar: return "script.compile";
        case ComandoCLI::EscenaScriptCompilar: return "scene.script.compile";
        case ComandoCLI::EscenaScriptRecargar: return "scene.script.reload";
        case ComandoCLI::EscenaUndo: return "scene.undo";
        case ComandoCLI::EscenaRedo: return "scene.redo";
        case ComandoCLI::EscenaAutoSave: return "scene.autosave";
        case ComandoCLI::EscenaObjetoSetField: return "scene.object.setfield";
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
        ComandoCLI::ProyectoSrcListar,
        ComandoCLI::ProyectoSrcBorrar,
        ComandoCLI::ProyectoSrcRenombrar,
        ComandoCLI::ProyectoDirImportar,
        ComandoCLI::ProyectoFsCrear,
        ComandoCLI::ArchivoCrear,
        ComandoCLI::EscenaListar,
        ComandoCLI::EscenaObjetos,
        ComandoCLI::EscenaObjetoObtener,
        ComandoCLI::EscenaObjetoPosicionGlobal,
        ComandoCLI::EscenaObjetoSetTransform,
        ComandoCLI::EscenaObjetoSetComponente,
        ComandoCLI::EscenaJerarquia,
        ComandoCLI::EscenaGuardar,
        ComandoCLI::EscenaObjetoSetPadre,
        ComandoCLI::EscenaObjetoAddComponente,
        ComandoCLI::EscenaObjetoCrear,
        ComandoCLI::EscenaObjetoClonar,
        ComandoCLI::EscenaObjetoQuitarComponente,
        ComandoCLI::EscenaObjetoDesvincularPadre,
        ComandoCLI::EscenaObjetoSetTag,
        ComandoCLI::EscenaObjetoSetActivo,
        ComandoCLI::EscenaObjetoComponentes,
        ComandoCLI::EscenaObjetoBuscarPorTag,
        ComandoCLI::EscenaObjetoBuscarPorNombre,
        ComandoCLI::EscenaObjetoGetGlobalTransform,
        ComandoCLI::AssetImportar,
        ComandoCLI::AssetValidar,
        ComandoCLI::AssetListarExtensiones,
        ComandoCLI::ScriptListar,
        ComandoCLI::ScriptCompilar,
        ComandoCLI::EscenaScriptCompilar,
        ComandoCLI::EscenaScriptRecargar,
        ComandoCLI::EscenaUndo,
        ComandoCLI::EscenaRedo,
        ComandoCLI::EscenaAutoSave,
        ComandoCLI::EscenaObjetoSetField,
        ComandoCLI::DiagnosticoListar,
        ComandoCLI::DiagnosticoLimpiar,
    };
    return lista;
}

}  // namespace funshi
