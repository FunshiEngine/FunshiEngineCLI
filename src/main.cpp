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
// Front-end de línea de comandos. Su única responsabilidad es parsear argv,
// leer la solicitud JSON (stdin o archivo), delegar en el núcleo
// (ejecutarSolicitud) y escribir la respuesta a stdout con el código de
// salida correspondiente. Toda la lógica de comandos vive en src/Comandos y
// src/Nucleo, de modo que una UI de escritorio puede llamar al núcleo sin
// pasar por este archivo.
#include "Nucleo/NucleoCLI.h"
#include "Nucleo/Registro.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"
#include "Tui/InterfazTui.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

// Escribe la respuesta a stdout en una sola línea.
void imprimirJson(const funshi::Respuesta& respuesta) {
    std::cout << respuesta.aTexto() << '\n';
}

// Construye una respuesta de error de uso (no se registra) y la imprime,
// devolviendo el código de salida indicado.
int errorUso(const std::string& codigo, const std::string& mensaje,
             int codigoSalida) {
    funshi::registrarErrorUso(codigo, mensaje);
    imprimirJson(funshi::Respuesta::fallo(codigo, mensaje));
    return codigoSalida;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Ayuda y versión en prosa (sin JSON) para uso interactivo.
    if (argc == 2) {
        const std::string argumento(argv[1]);
        if (argumento == "help" || argumento == "--help" || argumento == "-h") {
            std::cout << funshi::textoAyuda();
            return 0;
        }
        if (argumento == "version" || argumento == "--version") {
            std::cout << FUNSHI_VERSION << '\n';
            return 0;
        }
    }

    // Parseo de argumentos de línea de comandos.
    std::string archivoSolicitud;
    bool stdinSolicitud = false;
    bool modoTui = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argumento(argv[i]);
        if (argumento == "--project" && i + 1 < argc) {
            ++i;  // La resolución de --project se difiere al comando que la necesite.
        } else if (argumento == "--request") {
            stdinSolicitud = true;
        } else if (argumento == "--request-file" && i + 1 < argc) {
            archivoSolicitud = argv[++i];
        } else if (argumento == "--tui") {
            modoTui = true;
        } else {
            return errorUso("invalid_arguments",
                            "Argumento no reconocido o incompleto: " + argumento, 2);
        }
    }
    // La TUI toma la terminal completa y no admite solicitud adjunta.
    if (modoTui) {
        if (stdinSolicitud || !archivoSolicitud.empty()) {
            return errorUso("invalid_arguments",
                            "--tui no puede combinarse con --request o --request-file", 2);
        }
        // Sin terminal interactiva la TUI no tendría ninguna entrada: se
        // informa en vez de abrir una pantalla que no se puede manejar.
        const std::string errorEntrada = funshi::errorDeEntradaTui();
        if (!errorEntrada.empty()) {
            return errorUso("tui_terminal_unavailable", errorEntrada, 2);
        }
        return funshi::ejecutarTui();
    }
    if (stdinSolicitud == !archivoSolicitud.empty()) {
        return errorUso("invalid_arguments",
                        "Indica exactamente una fuente: --request o --request-file", 2);
    }

    // Lectura de la solicitud.
    std::string textoSolicitud;
    if (stdinSolicitud) {
        textoSolicitud.assign(std::istreambuf_iterator<char>(std::cin),
                              std::istreambuf_iterator<char>());
    } else {
        std::ifstream entrada(archivoSolicitud, std::ios::binary);
        if (!entrada) {
            return errorUso("request_file_unavailable",
                            "No se pudo leer el archivo de solicitud", 2);
        }
        textoSolicitud.assign(std::istreambuf_iterator<char>(entrada),
                              std::istreambuf_iterator<char>());
    }

    nlohmann::json jsonSolicitud;
    try {
        jsonSolicitud = nlohmann::json::parse(textoSolicitud);
    } catch (const nlohmann::json::exception& excepcion) {
        return errorUso("invalid_json", excepcion.what(), 2);
    }
    if (!jsonSolicitud.is_object() || !jsonSolicitud.contains("command") ||
        !jsonSolicitud["command"].is_string()) {
        return errorUso("invalid_request",
                        "La solicitud requiere un campo command de tipo string", 2);
    }

    funshi::Solicitud solicitud;
    solicitud.comando = jsonSolicitud["command"].get<std::string>();
    solicitud.argumentos = jsonSolicitud.value("args", nlohmann::json::object());
    if (!solicitud.argumentos.is_object()) {
        return errorUso("invalid_request", "args debe ser un objeto JSON", 2);
    }
    if (jsonSolicitud.contains("id")) {
        solicitud.id = jsonSolicitud["id"];
    }

    // Delegar en el núcleo y escribir la respuesta con su código de salida.
    funshi::ResultadoComando resultado = funshi::ejecutarSolicitud(solicitud);
    if (resultado.respuesta.id == std::nullopt && solicitud.id.has_value()) {
        resultado.respuesta.id = solicitud.id;
    }
    imprimirJson(resultado.respuesta);
    return resultado.codigoSalida;
}
