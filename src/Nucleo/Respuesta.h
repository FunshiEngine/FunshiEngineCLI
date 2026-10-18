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
#ifndef FUNSHI_NUCLEO_RESPUESTA_H
#define FUNSHI_NUCLEO_RESPUESTA_H

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

namespace funshi {

// Respuesta a una Solicitud. Es el resultado puro de la operación: no sabe
// nada de stdout ni de la consola, así que una UI puede consumirla igual que
// el front-end de línea de comandos.
struct Respuesta {
    bool correcta = false;
    nlohmann::json resultado;
    std::string codigoError;
    std::string mensajeError;
    std::optional<nlohmann::json> id;

    static Respuesta ok(nlohmann::json datos);
    static Respuesta fallo(std::string codigo, std::string mensaje);

    // Eco del identificador de la solicitud, si lo tenía.
    Respuesta& conId(std::optional<nlohmann::json> identificador);
    Respuesta& conId(const nlohmann::json& identificador);

    // Forma JSON de salida: {ok, result, error, apiVersion, id?}.
    nlohmann::json aJson() const;

    // Versión compacta en una línea, lista para escribirse a un stream.
    std::string aTexto() const;
};

// Versión de la API expuesta por la CLI. Se incrementa al agregar
// funcionalidad; los campos de respuesta nuevos van al final.
inline constexpr int kVersionApiCli = 1;

// Códigos de salida del proceso, compartidos por todos los comandos. Cada
// comando devuelve un ResultadoComando que empaqueta la Respuesta junto con
// el código de salida que debe devolver el proceso.
namespace codigosSalida {
// Operación completada con éxito.
inline constexpr int kExito = 0;
// Fallo general (p. ej. project_create_failed).
inline constexpr int kFalloGeneral = 1;
// Error de uso: la solicitud está mal formada o le faltan argumentos, o el
// comando es desconocido.
inline constexpr int kErrorUso = 2;
// El proyecto indicado no existe o no es válido.
inline constexpr int kProyectoInvalido = 3;
// Recurso origen/destino no encontrado, no es un archivo, o la ruta escapa.
inline constexpr int kRecursoNoEncontrado = 4;
// Falló una operación de escritura/creación/copia en disco.
inline constexpr int kFalloEscritura = 5;
}  // namespace codigosSalida

// Empareja una Respuesta con el código de salida que el proceso debe devolver.
// Es lo que produce cada manejador de comando y lo que consume el dispatcher
// para escribir la salida y salir.
struct ResultadoComando {
    Respuesta respuesta;
    int codigoSalida = codigosSalida::kFalloGeneral;

    // Fábrica para éxito.
    static ResultadoComando exito(nlohmann::json datos);
    // Fábrica para fallo, con el código de salida indicado.
    static ResultadoComando fallo(std::string codigo, std::string mensaje,
                                  int codigoSalida);
};

}  // namespace funshi

#endif  // FUNSHI_NUCLEO_RESPUESTA_H
