/*

    FunshiEngineGL - Motor de juegos 3D con OpenGL e ImGui
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
// Prueba headless del CLI (FunshiCLI): verifica respuesta JSON, eco de id,
// apiVersion en help, version y el recorrido de proyecto sobre un proyecto
// real.
//
// El binario del CLI se resuelve junto a este ejecutable (mismo directorio de
// build), con la extension .exe en Windows. Asi el test funciona en local y en
// CI sin rutas absolutas.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <array>
#include <memory>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <nlohmann/json.hpp>

#include "TempPruebas.h"

namespace fs = std::filesystem;

namespace {
int total = 0;
int fallos = 0;
#define CHECK(cond, msg) do { ++total; if (!(cond)) { ++fallos; std::cout << "FALLO: " << msg << " (linea " << __LINE__ << ")\\n"; } } while (0)

// Extrae el codigo de salida de std::system de forma multiplataforma. En
// Windows el valor devuelto ya es el exit code; en POSIX hay que desempaquetar
// el estado de espera.
int codigoSalidaSistema(int rc) {
#ifdef _WIN32
    return rc;
#else
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}

std::string ejecutarSistema(const std::string& cmd) {
    std::array<char, 4096> buffer;
    std::string resultado;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("Error al ejecutar: " + cmd);
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        resultado += buffer.data();
    }
    return resultado;
}

// Lee un archivo de texto completo.
std::string leerArchivo(const fs::path& ruta) {
    std::ifstream fh(ruta, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(fh)),
                       std::istreambuf_iterator<char>());
}

// Parsea un JSON sin lanzar excepcion: si el contenido no es JSON valido
// devuelve un valor nulo, que los comprobadores tratan como respuesta
// ausente.
nlohmann::json leerJson(const fs::path& ruta) {
    const std::string contenido = leerArchivo(ruta);
    if (contenido.empty()) {
        return nlohmann::json();
    }
    return nlohmann::json::parse(contenido, nullptr, false);
}

// Indica si una respuesta del CLI es satisfactoria.
bool esRespuestaOk(const nlohmann::json& j) {
    return j.is_object() && j.contains("ok") && j["ok"].is_boolean() &&
           j["ok"].get<bool>();
}

// Ejecuta el CLI con una peticion y deja la respuesta en un archivo. Devuelve
// el codigo de salida. La redireccion se escribe entre comillas porque la
// carpeta temporal puede estar en una ruta con espacios.
int ejecutarPeticion(const fs::path& bin, const fs::path& peticion,
                     const fs::path& salida) {
    const std::string cmd = bin.string() + " --request-file \"" + peticion.string() +
                            "\" > \"" + salida.string() + "\"";
    return codigoSalidaSistema(std::system(cmd.c_str()));
}

// Ruta del binario del CLI: mismo directorio que este ejecutable.
fs::path rutaBinarioCLI(const char* argv0) {
    fs::path dir = fs::absolute(argv0).parent_path();
#ifdef _WIN32
    return dir / "FunshiCLI.exe";
#else
    return dir / "FunshiCLI";
#endif
}
} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    TempPruebas::CarpetaPrueba tmpDir("cli-tests");

    if (argc < 1) {
        std::cerr << "Uso: cli-tests\n";
        return 1;
    }
    const fs::path bin = rutaBinarioCLI(argv[0]);

    // 1. help
    {
        fs::path f = tmpDir.ruta() / "help.json";
        {
            std::ofstream fh(f);
            fh << R"({"command":"help"})";
        }
        std::string cmd = bin.string() + " --request-file " + f.string();
        int rc = std::system(cmd.c_str());
        CHECK(codigoSalidaSistema(rc) == 0,
              "help: codigo de salida " + std::to_string(rc));
    }

    // 2. version
    {
        fs::path f = tmpDir.ruta() / "version.json";
        {
            std::ofstream fh(f);
            fh << R"({"command":"version"})";
        }
        std::string cmd = bin.string() + " --request-file " + f.string();
        int rc = std::system(cmd.c_str());
        CHECK(codigoSalidaSistema(rc) == 0,
              "version: codigo de salida " + std::to_string(rc));
    }

    // 3. eco de id
    {
        fs::path f = tmpDir.ruta() / "with-id.json";
        {
            std::ofstream fh(f);
            fh << R"({"command":"version","id":"123"})";
        }
        std::string cmd = bin.string() + " --request-file " + f.string();
        std::string salida = ejecutarSistema(cmd);

        CHECK(salida.find("\"apiVersion\":1") != std::string::npos,
              "respuesta debe tener apiVersion:1");
        CHECK(salida.find("\"id\":\"123\"") != std::string::npos,
              "respuesta debe incluir id:123");
    }

    // 4. --tui sin terminal interactiva. En Windows la TUI detecta que stdin
    // no es una terminal y se niega a arrancar con error de uso (codigo 2).
    // Se fuerza stdin desde NUL para que el resultado no dependa de la sesion
    // que lance el test. En POSIX la entrada la resuelve el propio FTXUI
    // (/dev/tty), asi que ahi no hay deteccion que comprobar.
#ifdef _WIN32
    {
        std::string cmd = bin.string() + " --tui < NUL";
        int rc = std::system(cmd.c_str());
        CHECK(codigoSalidaSistema(rc) == 2,
              "tui sin terminal: codigo de salida " + std::to_string(rc));
    }
#endif

    // 5. Recorrido de proyecto sobre un proyecto real: project.list carga la
    // configuracion de cada proyecto encontrado y project.config.get carga la
    // de uno concreto. Ambas pasan por ConfigPersistence::cargarProyecto(),
    // que devuelve un struct por valor: si las cabeceras con las que compila
    // la CLI y la biblioteca que enlaza no coinciden, ese valor se libera
    // con un layout distinto del reservado y el proceso se corrompe. Por eso
    // el recorrido necesita un proyecto de verdad en la lista: con la lista
    // vacia el camino no se ejecuta.
    {
        const std::string nombre = "CliTestProyectoTemporal";
        const fs::path fCreate = tmpDir.ruta() / "create.json";
        const fs::path fList = tmpDir.ruta() / "list.json";
        const fs::path fConfig = tmpDir.ruta() / "config.json";
        const fs::path fSalida = tmpDir.ruta() / "salida.json";
        std::string rutaProyecto;

        {
            std::ofstream fh(fCreate);
            fh << R"({"command":"project.create","args":{"name":"CliTestProyectoTemporal"}})";
        }
        {
            std::ofstream fh(fList);
            fh << R"({"command":"project.list"})";
        }
        {
            std::ofstream fh(fConfig);
            fh << R"({"command":"project.config.get","args":{"project":"CliTestProyectoTemporal"}})";
        }

        // project.list: comprueba que responde y que el proyecto temporal
        // aparece en la lista. Si una corrida anterior dejo el proyecto
        // creado, se borra antes para que el recorrido sea repetible.
        int rc = ejecutarPeticion(bin, fList, fSalida);
        CHECK(rc == 0, "project.list: codigo de salida " + std::to_string(rc));
        nlohmann::json jList = leerJson(fSalida);
        CHECK(esRespuestaOk(jList), "project.list: la respuesta debe ser satisfactoria");
        if (jList.is_object() && jList.contains("result") &&
            jList["result"].is_object() && jList["result"].contains("projects") &&
            jList["result"]["projects"].is_array()) {
            for (const auto& p : jList["result"]["projects"]) {
                if (p.is_object() && p.value("name", std::string()) == nombre &&
                    p.contains("path") && p["path"].is_string()) {
                    rutaProyecto = p["path"].get<std::string>();
                }
            }
        }
        if (!rutaProyecto.empty()) {
            std::error_code ec;
            fs::remove_all(fs::path(rutaProyecto), ec);
        }

        // project.create: deja el proyecto disponible para el resto del
        // recorrido.
        rc = ejecutarPeticion(bin, fCreate, fSalida);
        CHECK(rc == 0, "project.create: codigo de salida " + std::to_string(rc));
        nlohmann::json jCreate = leerJson(fSalida);
        CHECK(esRespuestaOk(jCreate), "project.create: la respuesta debe ser satisfactoria");
        if (jCreate.is_object() && jCreate.contains("result") &&
            jCreate["result"].is_object() && jCreate["result"].contains("path") &&
            jCreate["result"]["path"].is_string()) {
            rutaProyecto = jCreate["result"]["path"].get<std::string>();
        }

        // project.list: el proyecto recien creado tiene que aparecer.
        rc = ejecutarPeticion(bin, fList, fSalida);
        CHECK(rc == 0, "project.list: codigo de salida " + std::to_string(rc));
        jList = leerJson(fSalida);
        CHECK(esRespuestaOk(jList), "project.list: la respuesta debe ser satisfactoria");
        bool encontrado = false;
        if (jList.is_object() && jList.contains("result") &&
            jList["result"].is_object() && jList["result"].contains("projects") &&
            jList["result"]["projects"].is_array()) {
            for (const auto& p : jList["result"]["projects"]) {
                if (p.is_object() && p.value("name", std::string()) == nombre) {
                    encontrado = true;
                }
            }
        }
        CHECK(encontrado, "project.list: debe listar el proyecto creado");

        // project.config.get: lee la configuracion del proyecto creado.
        rc = ejecutarPeticion(bin, fConfig, fSalida);
        CHECK(rc == 0, "project.config.get: codigo de salida " + std::to_string(rc));
        nlohmann::json jConfig = leerJson(fSalida);
        CHECK(esRespuestaOk(jConfig), "project.config.get: la respuesta debe ser satisfactoria");
        if (jConfig.is_object() && jConfig.contains("result") &&
            jConfig["result"].is_object()) {
            CHECK(jConfig["result"].value("name", std::string()) == nombre,
                  "project.config.get: debe devolver el proyecto pedido");
        }

        // Limpieza: el proyecto temporal no debe quedar en el directorio de
        // proyectos del motor.
        if (!rutaProyecto.empty()) {
            std::error_code ec;
            fs::remove_all(fs::path(rutaProyecto), ec);
            CHECK(!fs::exists(fs::path(rutaProyecto)),
                  "el proyecto temporal debe quedar borrado");
        }
    }

    std::cout << (fallos == 0 ? "OK" : "FALLOS") << ": "
              << total << " comprobaciones, " << fallos << " fallos\n";
    return fallos == 0 ? 0 : 1;
}
