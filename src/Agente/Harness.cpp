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
#include <filesystem>
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
    // "<proyecto>" es el marcador de los ejemplos, no un nombre: cuenta como ausente.
    const bool marcador = valor.size() > 2 && valor.front() == '<' && valor.back() == '>';
    if (!valor.empty() && !marcador) {
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

void Harness::normalizarRutasSrc(Solicitud& solicitud) const {
    if (!correccionesAgente_ || solicitud.comando.rfind("project.src.", 0) != 0 || !solicitud.argumentos.is_object()) return;
    for (const char* clave : {"path", "newPath"}) {
        auto it = solicitud.argumentos.find(clave);
        if (it == solicitud.argumentos.end() || !it->is_string()) continue;
        std::string ruta = it->get<std::string>();
        while (ruta.rfind("./", 0) == 0) ruta.erase(0, 2);
        if (ruta.size() > 4 && (ruta.rfind("src/", 0) == 0 || ruta.rfind("src\\", 0) == 0)) ruta.erase(0, 4);
        *it = ruta;
    }
    // "name" con "path" de carpeta (el formato de file.create): la ruta del
    // archivo es la carpeta más el nombre. "name" no es argumento del comando.
    auto& a = solicitud.argumentos;
    if (solicitud.comando == "project.src.create" && a.contains("name") && a["name"].is_string()) {
        const std::string nombre = a["name"].get<std::string>();
        std::string ruta = a.value("path", std::string());
        if (!nombre.empty() && (ruta.empty() || ruta.back() == '/' || ruta.back() == '\\' ||
                                std::filesystem::path(ruta).extension().empty())) {
            if (!ruta.empty() && ruta.back() != '/' && ruta.back() != '\\') ruta += '/';
            a["path"] = ruta + nombre;
        }
        a.erase("name");
    }
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

    Solicitud efectiva = conProyectoPorDefecto(solicitud);
    if (correccionesAgente_ && efectiva.argumentos.is_object()) {
        // Un argumento en null es un argumento ausente (los comandos esperan
        // el tipo declarado o nada).
        for (auto it = efectiva.argumentos.begin(); it != efectiva.argumentos.end();) {
            it = it->is_null() ? efectiva.argumentos.erase(it) : std::next(it);
        }
    }
    normalizarRutasSrc(efectiva);
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
