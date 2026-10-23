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
#ifndef FUNSHI_NUCLEO_CATALOGO_COMANDOS_H
#define FUNSHI_NUCLEO_CATALOGO_COMANDOS_H

#include <string>
#include <vector>

namespace funshi {

// Catálogo cerrado de comandos. El enum con switch exhaustivo le permite al
// compilador avisar de comandos sin cubrir al agregar valores, y evita
// errores de typo en la comparación por texto.
enum class ComandoCLI {
    Ayuda,
    Version,
    ProyectoCrear,
    ProyectoListar,
    ProyectoConfigLeer,
    ProyectoSrcCrear,
    ProyectoSrcListar,
    ProyectoSrcBorrar,
    ProyectoSrcRenombrar,
    ProyectoDirImportar,
    ProyectoFsCrear,
    ArchivoCrear,
    EscenaListar,
    EscenaObjetos,
    EscenaObjetoObtener,
    EscenaObjetoPosicionGlobal,
    EscenaObjetoSetTransform,
    EscenaObjetoSetComponente,
    EscenaJerarquia,
    EscenaGuardar,
    EscenaObjetoSetPadre,
    EscenaObjetoAddComponente,
    EscenaObjetoCrear,
    EscenaObjetoClonar,
    EscenaObjetoQuitarComponente,
    EscenaObjetoDesvincularPadre,
    EscenaObjetoSetTag,
    EscenaObjetoSetActivo,
    EscenaObjetoComponentes,
    EscenaObjetoBuscarPorTag,
    EscenaObjetoBuscarPorNombre,
    EscenaObjetoGetGlobalTransform,
    AssetImportar,
    AssetValidar,
    AssetListarExtensiones,
    ScriptListar,
    ScriptCompilar,
    EscenaScriptCompilar,
    EscenaScriptRecargar,
    EscenaUndo,
    EscenaRedo,
    EscenaAutoSave,
    EscenaObjetoSetField,
    DiagnosticoListar,
    DiagnosticoLimpiar,
    Desconocido
};

// Mapea el texto del comando a su valor del enum; Desconocido si nadie lo
// reconoce.
ComandoCLI comandoDesdeTexto(const std::string& texto);

// Texto canónico del comando (el que aparece en la solicitud).
std::string textoDesdeComando(ComandoCLI comando);

// Todos los comandos soportados, en el orden del catálogo.
const std::vector<ComandoCLI>& todosLosComandos();

}  // namespace funshi

#endif  // FUNSHI_NUCLEO_CATALOGO_COMANDOS_H
