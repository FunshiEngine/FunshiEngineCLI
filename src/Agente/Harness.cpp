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
#include "Agente/Conocimiento/GuiaScripts.h"

#include <Configuracion/ProjectPaths.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

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

namespace {

// true si `nuevo` conserva todas las líneas no vacías del archivo (agregar
// un método, por ejemplo): reemplazarlo no pierde nada.
bool conservaContenido(const std::filesystem::path& archivo, const std::string& nuevo) {
    std::ifstream f(archivo, std::ios::binary);
    if (!f) return false;
    const auto recortada = [](std::string l) {
        const auto ini = l.find_first_not_of(" \t\r");
        if (ini == std::string::npos) return std::string();
        return l.substr(ini, l.find_last_not_of(" \t\r") - ini + 1);
    };
    std::multiset<std::string> lineasNuevas;
    {
        std::istringstream n(nuevo);
        for (std::string l; std::getline(n, l);) {
            if (auto r = recortada(l); !r.empty()) lineasNuevas.insert(r);
        }
    }
    for (std::string l; std::getline(f, l);) {
        const auto r = recortada(l);
        if (r.empty()) continue;
        const auto it = lineasNuevas.find(r);
        if (it == lineasNuevas.end()) return false;
        lineasNuevas.erase(it);
    }
    return true;
}

}  // namespace

ResultadoComando* Harness::rechazoDeArchivo(const Solicitud& solicitud, ResultadoComando& destino) const {
    if (!correccionesAgente_ || solicitud.comando != "project.src.create" || !solicitud.argumentos.is_object()) return nullptr;
    const auto& a = solicitud.argumentos;
    const std::string proyecto = a.value("project", std::string());
    const std::string ruta = a.value("path", std::string());
    if (!a.value("file", false) || proyecto.empty() || ruta.empty()) return nullptr;
    if (!a.value("content", std::string()).empty() && std::filesystem::path(ruta).extension().empty()) {
        // Sugerencia con la ruta tal cual más una extensión: "path" es la ruta
        // completa del archivo, no la carpeta.
        std::string base = ruta;
        while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
        if (base.empty()) base = "Scripts/Archivo";
        destino = ResultadoComando::fallo("missing_extension",
                                          "\"" + ruta + "\" no tiene extensión. \"path\" es la ruta completa del archivo "
                                          "con la extensión de su lenguaje, por ejemplo \"" + base + ".java\" o \"" + base +
                                              ".cpp\".",
                                          codigosSalida::kErrorUso);
        return &destino;
    }
    // Un script con la estructura de otro lado no lo carga el motor: el error
    // trae la plantilla del motor para esa clase.
    const std::string contenido = a.value("content", std::string());
    if (const std::string problema = contenido.empty() ? std::string() : agente::conocimiento::problemaDeScript(ruta, contenido);
        !problema.empty()) {
        destino = ResultadoComando::fallo("invalid_script",
                                          "El script no sirve para el motor: " + problema +
                                              ". Estructura que espera el motor:\n" +
                                              agente::conocimiento::plantillaDeScript(ruta),
                                          codigosSalida::kErrorUso);
        return &destino;
    }
    std::error_code ec;
    const std::filesystem::path completa = std::filesystem::path(ProjectPaths::directorioSrc(proyecto)) / ruta;
    if (a.value("sobrescribir", false) || !std::filesystem::is_regular_file(completa, ec)) return nullptr;
    if (conservaContenido(completa, a.value("content", std::string()))) return nullptr;
    destino = ResultadoComando::fallo("file_exists",
                                      "Ya existe " + ruta + " y su contenido se perdería. Si el usuario pidió "
                                      "cambiarlo, repite la llamada con \"sobrescribir\": true y el contenido "
                                      "completo; si no, no lo toques.",
                                      codigosSalida::kErrorUso);
    return &destino;
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
        // "true"/"false" escritos como texto en un argumento booleano (el
        // comando espera el tipo y, si no, termina en una excepción de JSON).
        if (const AyudaComando* ayuda = buscarAyudaComando(efectiva.comando)) {
            for (const auto& arg : ayuda->argumentos) {
                auto it = efectiva.argumentos.find(arg.nombre);
                if (it == efectiva.argumentos.end() || !it->is_string() || arg.tipo.find("true") == std::string::npos) continue;
                const std::string v = it->get<std::string>();
                if (v == "true" || v == "false") *it = (v == "true");
            }
        }
    }
    normalizarRutasSrc(efectiva);
    ResultadoComando rechazo;
    const ResultadoComando* motivo = rechazoDeArchivo(efectiva, rechazo);
    if (efectiva.argumentos.is_object()) efectiva.argumentos.erase("sobrescribir");
    ResultadoComando resultado = motivo ? *motivo : ejecutarSolicitud(efectiva);

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
