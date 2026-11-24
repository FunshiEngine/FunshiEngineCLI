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
#include "Agente/Harness.h"

#include "Nucleo/AyudaComandos.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace funshi {

namespace {

std::string enMinusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

Harness::Harness(Auditoria& auditoria) : auditoria_(auditoria) {}

std::string Harness::proyectoReconocido(const std::string& valor, const std::vector<std::string>& conocidos) {
    const auto coincide = [&](const std::string& parte) -> std::string {
        const std::string bajo = enMinusculas(parte);
        for (const auto& nombre : conocidos) {
            if (!nombre.empty() && enMinusculas(nombre) == bajo) return nombre;
        }
        return {};
    };
    if (auto n = coincide(valor); !n.empty()) return n;
    // Ruta: se mira cada tramo, de atrás hacia adelante; "srcX" es la carpeta
    // de fuentes del proyecto X.
    std::vector<std::string> tramos;
    std::string actual;
    for (char c : valor) {
        if (c == '/' || c == '\\') {
            if (!actual.empty()) tramos.push_back(actual);
            actual.clear();
        } else {
            actual += c;
        }
    }
    if (!actual.empty()) tramos.push_back(actual);
    if (tramos.size() < 2) return {};
    for (auto t = tramos.rbegin(); t != tramos.rend(); ++t) {
        if (auto n = coincide(*t); !n.empty()) return n;
        if (t->size() > 3 && enMinusculas(t->substr(0, 3)) == "src") {
            if (auto n = coincide(t->substr(3)); !n.empty()) return n;
        }
    }
    return {};
}

Solicitud Harness::conProyectoPorDefecto(const Solicitud& solicitud) const {
    if (!solicitud.argumentos.is_object()) return solicitud;
    const AyudaComando* ayuda = buscarAyudaComando(solicitud.comando);
    if (!ayuda || !ayuda->usaProyecto) return solicitud;
    const auto it = solicitud.argumentos.find("project");
    const std::string valor = it != solicitud.argumentos.end() && it->is_string() ? it->get<std::string>() : "";
    if (!valor.empty()) {
        // Un nombre inexistente que se refiere a uno real (ruta, mayúsculas)
        // se corrige; uno que no se reconoce llega tal cual y el comando
        // responde que no existe.
        const bool existe = std::find(proyectosConocidos_.begin(), proyectosConocidos_.end(), valor) !=
                            proyectosConocidos_.end();
        if (existe) return solicitud;
        const std::string real = proyectoReconocido(valor, proyectosConocidos_);
        if (real.empty()) return solicitud;
        Solicitud copia = solicitud;
        copia.argumentos["project"] = real;
        return copia;
    }
    if (proyectoPorDefecto_.empty()) return solicitud;
    Solicitud copia = solicitud;
    copia.argumentos["project"] = proyectoPorDefecto_;
    return copia;
}

ResultadoComando Harness::ejecutar(const Solicitud& solicitud,
                                   bool modificaDisco) {
    // Un comando a la vez en todo el proceso: varias sesiones de chat y
    // subagentes en paralelo comparten el motor (escenas, rutas, registro de
    // diagnósticos) y la auditoría, que no son seguros entre hilos. Los
    // comandos tardan milisegundos; lo lento es el modelo, que sí corre en
    // paralelo. Recursivo: agente.procesar vuelve a entrar desde el mismo hilo.
    static auto* mutexComandos = new std::recursive_mutex();  // nunca se destruye (hilos)
    std::lock_guard<std::recursive_mutex> bloqueo(*mutexComandos);
    if (!modificaDisco) {
        modificaDisco = auditoria_.esComandoDeEscritura(solicitud.comando);
    }

    const Solicitud efectiva = conProyectoPorDefecto(solicitud);
    ResultadoComando resultado = ejecutarSolicitud(efectiva);

    EntradaAuditoria entrada;
    entrada.marcaTiempo = auditoria_.ahoraIso8601();
    entrada.comando = solicitud.comando;
    entrada.argumentos = efectiva.argumentos;
    entrada.ok = resultado.respuesta.correcta;
    entrada.codigoError = resultado.respuesta.codigoError;
    entrada.mensajeError = resultado.respuesta.mensajeError;
    entrada.codigoSalida = resultado.codigoSalida;
    entrada.modificaDisco = modificaDisco;
    if (resultado.respuesta.id) {
        entrada.observaciones = "id=" + resultado.respuesta.id->dump();
    }

    auditoria_.registrar(entrada);
    return resultado;
}

ResultadoComando Harness::ejecutarTexto(const std::string& comandoTexto,
                                        const nlohmann::json& argumentos,
                                        bool modificaDisco) {
    Solicitud solicitud;
    solicitud.comando = comandoTexto;
    solicitud.argumentos = argumentos;
    return ejecutar(solicitud, modificaDisco);
}

}  // namespace funshi
