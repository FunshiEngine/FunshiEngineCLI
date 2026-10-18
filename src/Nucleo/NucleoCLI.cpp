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
#include "Nucleo/NucleoCLI.h"

#include "Comandos/ComandosArchivos.h"
#include "Comandos/ComandosAssets.h"
#include "Comandos/ComandosDiagnosticos.h"
#include "Comandos/ComandosEscena.h"
#include "Comandos/ComandosProyecto.h"
#include "Comandos/ComandosScripts.h"
#include "Nucleo/CatalogoComandos.h"
#include "Nucleo/Registro.h"

#ifndef FUNSHI_VERSION
#define FUNSHI_VERSION "0.0.0"
#endif

namespace funshi {

namespace {

// Versión de la CLI para el comando version y la ayuda.
ResultadoComando respuestaVersion() {
    return ResultadoComando::exito({{"version", FUNSHI_VERSION}});
}

// Lista de comandos en JSON, para el comando help.
ResultadoComando respuestaAyudaComandos() {
    nlohmann::json listaComandos = nlohmann::json::array();
    for (ComandoCLI comando : todosLosComandos()) {
        listaComandos.push_back(textoDesdeComando(comando));
    }
    return ResultadoComando::exito({
        {"commands", std::move(listaComandos)},
        {"request", "JSON por stdin (--request) o archivo (--request-file)"}
    });
}

}  // namespace

ResultadoComando ejecutarSolicitud(const Solicitud& solicitud) {
    // Los errores de uso se devuelven solo como respuesta; los operacionales
    // también se registran en el historial compartido.
    const nlohmann::json& argumentos = solicitud.argumentos;

    switch (comandoDesdeTexto(solicitud.comando)) {
        case ComandoCLI::Ayuda:
            return respuestaAyudaComandos();
        case ComandoCLI::Version:
            return respuestaVersion();
        case ComandoCLI::ProyectoCrear:
            return proyectoCrear(argumentos);
        case ComandoCLI::ProyectoListar:
            return proyectoListar(argumentos);
        case ComandoCLI::ProyectoConfigLeer:
            return proyectoConfigLeer(argumentos);
        case ComandoCLI::ProyectoSrcCrear:
            return proyectoSrcCrear(argumentos);
        case ComandoCLI::ProyectoDirImportar:
            return proyectoDirImportar(argumentos);
        case ComandoCLI::ProyectoFsCrear:
            return proyectoFsCrear(argumentos);
        case ComandoCLI::ArchivoCrear:
            return archivoCrear(argumentos);
        case ComandoCLI::EscenaListar:
            return escenaListar(argumentos);
        case ComandoCLI::EscenaObjetos:
            return escenaObjetos(argumentos);
        case ComandoCLI::EscenaObjetoObtener:
            return escenaObjetoObtener(argumentos);
        case ComandoCLI::EscenaJerarquia:
            return escenaJerarquia(argumentos);
        case ComandoCLI::AssetImportar:
            return assetImportar(argumentos);
        case ComandoCLI::ScriptListar:
            return scriptListar(argumentos);
        case ComandoCLI::ScriptCompilar:
            return scriptCompilar(argumentos);
        case ComandoCLI::DiagnosticoListar:
            return diagnosticoListar(argumentos);
        case ComandoCLI::DiagnosticoLimpiar:
            return diagnosticoLimpiar(argumentos);
        case ComandoCLI::Desconocido: {
            const std::string mensaje = "Comando no encontrado: " + solicitud.comando;
            registrarErrorUso("unknown_command", mensaje);
            return ResultadoComando::fallo("unknown_command", mensaje,
                                           codigosSalida::kErrorUso);
        }
    }
    // inalcanzable con switch exhaustivo; se deja por robustez.
    return ResultadoComando::fallo("unknown_command", "Comando no reconocido",
                                   codigosSalida::kErrorUso);
}

std::string textoAyuda() {
    return std::string("Funshi CLI ") + FUNSHI_VERSION + "\n" +
           "Uso:\n" +
           "  FunshiCLI help | version\n" +
           "  FunshiCLI --tui\n" +
           "  FunshiCLI [--project RUTA] --request\n" +
           "  FunshiCLI [--project RUTA] --request-file ARCHIVO.json\n" +
           "La solicitud JSON se envia por stdin o desde un archivo. --tui abre\n" +
           "la interfaz interactiva de terminal.\n" +
           "Comandos: help, version, project.create, project.list, "
           "project.config.get, project.src.create, project.dir.import, "
           "scene.list, scene.objects, scene.object.get, asset.import, "
           "script.list, script.compile, diagnostics.list, diagnostics.clear\n";
}

}  // namespace funshi
