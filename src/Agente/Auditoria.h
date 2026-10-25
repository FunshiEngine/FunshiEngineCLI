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
#ifndef FUNSHI_AGENTE_AUDITORIA_H
#define FUNSHI_AGENTE_AUDITORIA_H

#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

#include <chrono>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace funshi {

// Registro de una ejecución realizada por el agente Silvy.
// Permite auditar qué comando se pidió, con qué argumentos, qué respondió el
// núcleo y si se trató de un cambio sobre el sistema de archivos.
struct EntradaAuditoria {
    std::string marcaTiempo;           // ISO 8601
    std::string comando;
    nlohmann::json argumentos;
    bool ok = false;
    std::string codigoError;
    std::string mensajeError;
    int codigoSalida = 0;
    bool modificaDisco = false;        // heurística: el comando escribe en disco
    std::string observaciones;         // notas libres del agente/harness
};

// Auditoria centralizada: escribe un archivo de log legible y mantiene las
// entradas en memoria para consultas rápidas (historial del chat).
class Auditoria {
public:
    explicit Auditoria(std::string rutaLog = ".agentes/silvy-auditoria.log");

    // Registra una ejecución y la añade al historial en memoria.
    void registrar(const EntradaAuditoria& entrada);

    // Devuelve todas las entradas registradas (más recientes al final).
    const std::vector<EntradaAuditoria>& entradas() const;

    // Devuelve solo las entradas que marcan cambios en disco.
    std::vector<EntradaAuditoria> cambiosEnDisco() const;

    // Escribe un resumen legible de las últimas n entradas.
    std::string resumenReciente(std::size_t n = 10) const;

public:
    // Devuelve la marca de tiempo actual en ISO 8601 (útil para el llamador).
    std::string ahoraIso8601() const;
    // Indica si un comando, por su nombre, tiende a escribir en disco.
    bool esComandoDeEscritura(const std::string& comando) const;

private:
    std::string rutaLog_;
    std::vector<EntradaAuditoria> historial_;
    mutable std::mutex mutex_;

    void escribirLineaLog(const EntradaAuditoria& entrada);
};

}  // namespace funshi

#endif  // FUNSHI_AGENTE_AUDITORIA_H
