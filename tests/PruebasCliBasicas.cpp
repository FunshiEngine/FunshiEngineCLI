/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// cli-tests: comandos básicos (help, version, id, TUI sin terminal) y
// recorrido de proyecto sobre un proyecto real.
#include "PruebasCli.h"

#include <cstdlib>
#include <fstream>

namespace pruebas_cli {

void probarComandosBasicos(const fs::path& bin, const fs::path& tmp) {
    // 1. help
    {
        fs::path f = tmp / "help.json";
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
        fs::path f = tmp / "version.json";
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
        fs::path f = tmp / "with-id.json";
        {
            std::ofstream fh(f);
            fh << R"({"command":"version","id":"123"})";
        }
        std::string cmd = bin.string() + " --request-file " + f.string();
        std::string salida = ejecutarSistema(cmd);

        CHECK(salida.find("\"apiVersion\":2") != std::string::npos,
              "respuesta debe tener apiVersion:2");
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
}

void probarRecorridoProyecto(const fs::path& bin, const fs::path& tmp) {
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
        const fs::path fCreate = tmp / "create.json";
        const fs::path fList = tmp / "list.json";
        const fs::path fConfig = tmp / "config.json";
        const fs::path fSalida = tmp / "salida.json";
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
}

}  // namespace pruebas_cli
