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
#include "Agente/Auditoria.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sstream>

namespace funshi {

namespace {

// Comandos que, por su nombre, escriben o crean algo en disco.
// Se usa como heurística para marcar entradas de auditoría.
const std::vector<std::string>& comandosDeEscritura() {
    static const std::vector<std::string> lista = {
        "project.create",
        "project.src.create",
        "project.src.delete",
        "project.src.rename",
        "project.dir.import",
        "project.fs.create",
        "file.create",
        "scene.object.create",
        "scene.object.clone",
        "scene.object.addcomponent",
        "scene.object.removecomponent",
        "scene.object.setparent",
        "scene.object.unparent",
        "scene.object.setfield",
        "scene.object.settransform",
        "scene.object.settag",
        "scene.object.setactive",
        "scene.save",
        "scene.undo",
        "scene.redo",
        "scene.script.compile",
        "scene.script.reload",
        "asset.import",
    };
    return lista;
}

}  // namespace

Auditoria::Auditoria(std::string rutaLog) : rutaLog_(std::move(rutaLog)) {}

std::string Auditoria::ahoraIso8601() const {
    auto ahora = std::chrono::system_clock::now();
    std::time_t tiempo = std::chrono::system_clock::to_time_t(ahora);
    std::tm tmLocal{};
#ifdef _WIN32
    localtime_s(&tmLocal, &tiempo);
#else
    localtime_r(&tiempo, &tmLocal);
#endif
    std::ostringstream os;
    os << std::put_time(&tmLocal, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

bool Auditoria::esComandoDeEscritura(const std::string& comando) const {
    for (const std::string& c : comandosDeEscritura()) {
        if (comando == c) {
            return true;
        }
    }
    return false;
}

void Auditoria::escribirLineaLog(const EntradaAuditoria& entrada) {
    std::ofstream archivo(rutaLog_, std::ios::app);
    if (!archivo) {
        return;  // Si no se puede escribir el log, no se bloquea la ejecución.
    }
    nlohmann::json linea;
    linea["marcaTiempo"] = entrada.marcaTiempo;
    linea["comando"] = entrada.comando;
    linea["argumentos"] = entrada.argumentos;
    linea["ok"] = entrada.ok;
    if (!entrada.codigoError.empty()) {
        linea["error"] = entrada.codigoError;
    }
    if (!entrada.mensajeError.empty()) {
        linea["mensaje"] = entrada.mensajeError;
    }
    linea["codigoSalida"] = entrada.codigoSalida;
    linea["modificaDisco"] = entrada.modificaDisco;
    if (!entrada.observaciones.empty()) {
        linea["observaciones"] = entrada.observaciones;
    }
    archivo << linea.dump() << '\n';
}

void Auditoria::registrar(const EntradaAuditoria& entrada) {
    std::lock_guard<std::mutex> bloqueo(mutex_);
    historial_.push_back(entrada);
    escribirLineaLog(entrada);
}

const std::vector<EntradaAuditoria>& Auditoria::entradas() const {
    return historial_;
}

std::vector<EntradaAuditoria> Auditoria::cambiosEnDisco() const {
    std::lock_guard<std::mutex> bloqueo(mutex_);
    std::vector<EntradaAuditoria> resultado;
    for (const EntradaAuditoria& e : historial_) {
        if (e.modificaDisco) {
            resultado.push_back(e);
        }
    }
    return resultado;
}

std::string Auditoria::resumenReciente(std::size_t n) const {
    std::lock_guard<std::mutex> bloqueo(mutex_);
    std::ostringstream os;
    if (historial_.empty()) {
        os << "Sin entradas de auditoría.\n";
        return os.str();
    }
    std::size_t inicio = (historial_.size() > n) ? historial_.size() - n : 0;
    os << "Últimas " << (historial_.size() - inicio) << " ejecuciones de Silvy:\n";
    for (std::size_t i = inicio; i < historial_.size(); ++i) {
        const EntradaAuditoria& e = historial_[i];
        os << "  [" << e.marcaTiempo << "] " << e.comando;
        if (!e.argumentos.empty() && e.argumentos != nlohmann::json::object()) {
            os << " args=" << e.argumentos.dump();
        }
        os << " -> " << (e.ok ? "ok" : ("error " + e.codigoError));
        os << " (salida=" << e.codigoSalida << ")";
        if (e.modificaDisco) {
            os << " [CAMBIO EN DISCO]";
        }
        if (!e.observaciones.empty()) {
            os << " | " << e.observaciones;
        }
        os << '\n';
    }
    return os.str();
}

}  // namespace funshi
