/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// cli-tests: rutas configurables, validación de argumentos, persistencia de
// escena y agente, todo en un directorio de trabajo aislado.
#include "PruebasCli.h"

namespace pruebas_cli {

void probarConfiguracion(const EntornoAislado& env) {
    auto ejecutar = [&](const std::string& nombre, const nlohmann::json& solicitud, int& rc) {
        return env.ejecutar(nombre, solicitud, rc);
    };
    const fs::path& trabajo = env.trabajo;
    const fs::path datos = trabajo / "datos";
    (void)datos;
    int rc = 0;
    nlohmann::json j = ejecutar("config-set",
        solicitudJson("cli.config.set", {
            {"projectsDataDir", datos.string()},
            {"localInferenceDir", (trabajo / "inferencia").string()},
            {"modelsDir", (trabajo / "modelos").string()},
            {"hfCacheDir", (trabajo / "hf").string()},
            {"llamaRepoDir", (trabajo / "llama").string()},
        }), rc);
    CHECK(rc == 0, "cli.config.set: codigo de salida");
    CHECK(esRespuestaOk(j), "cli.config.set: respuesta satisfactoria");
    CHECK(fs::exists(trabajo / ".agentes" / "llm-config.json"),
          "cli.config.set: el archivo de configuracion debe persistirse");

    j = ejecutar("config-get", solicitudJson("cli.config.get"), rc);
    CHECK(rc == 0, "cli.config.get: codigo de salida");
    CHECK(esRespuestaOk(j), "cli.config.get: respuesta satisfactoria");
    if (esRespuestaOk(j) && j.contains("result") && j["result"].is_object()) {
        const auto paths = j["result"].value("paths", nlohmann::json::object());
        const auto efectivas = j["result"].value("effectivePaths", nlohmann::json::object());
        CHECK(paths.value("projectsDataDir", std::string()) == datos.string(),
              "cli.config.get: projectsDataDir persistida");
        CHECK(efectivas.value("modelsDir", std::string()) == (trabajo / "modelos").string(),
              "cli.config.get: modelsDir efectiva");
        CHECK(efectivas.value("hfCacheDir", std::string()) == (trabajo / "hf").string(),
              "cli.config.get: hfCacheDir efectiva");
    } else {
        CHECK(false, "cli.config.get: debe devolver resultado con rutas");
    }

    // Contexto del modelo: 32768 por defecto, editable y validado.
    if (esRespuestaOk(j)) {
        CHECK(j["result"].value("settings", nlohmann::json::object())
                      .value("contextSize", 0) == 32768,
              "cli.config.get: contextSize por defecto 32768");
    }
    j = ejecutar("config-ctx", solicitudJson("cli.config.set", {{"contextSize", 16384}}), rc);
    CHECK(rc == 0 && esRespuestaOk(j), "cli.config.set contextSize: respuesta satisfactoria");
    j = ejecutar("config-ctx-get", solicitudJson("cli.config.get"), rc);
    CHECK(esRespuestaOk(j) &&
              j["result"].value("settings", nlohmann::json::object())
                      .value("contextSize", 0) == 16384,
          "cli.config.get: contextSize persistido");
    j = ejecutar("config-ctx-mal", solicitudJson("cli.config.set", {{"contextSize", 10}}), rc);
    CHECK(rc == 2 && !esRespuestaOk(j), "cli.config.set contextSize fuera de rango: error de uso");
}

void probarEscenaYArchivos(const EntornoAislado& env) {
    auto ejecutar = [&](const std::string& nombre, const nlohmann::json& solicitud, int& rc) {
        return env.ejecutar(nombre, solicitud, rc);
    };
    const fs::path& trabajo = env.trabajo;
    const fs::path datos = trabajo / "datos";
    (void)datos;
    int rc = 0;
    nlohmann::json j;
    j = ejecutar("proyecto-crear", solicitudJson("project.create", {
        {"name", "Aislado"}, {"dimension", "3d"}
    }), rc);
    CHECK(rc == 0, "project.create aislado: codigo de salida");
    CHECK(esRespuestaOk(j), "project.create aislado: respuesta satisfactoria");
    if (esRespuestaOk(j) && j.contains("result") &&
        j["result"].contains("path") && j["result"]["path"].is_string()) {
        const fs::path rutaProyecto = j["result"]["path"].get<std::string>();
        CHECK(rutaProyecto.string().find(datos.string()) == 0,
              "project.create: el proyecto debe crearse bajo la ruta configurada");
    }

    j = ejecutar("objeto-crear", solicitudJson("scene.object.create", {
        {"project", "Aislado"}, {"name", "Cubo"}
    }), rc);
    CHECK(rc == 0, "scene.object.create: codigo de salida");
    CHECK(esRespuestaOk(j), "scene.object.create: respuesta satisfactoria");
    int id = -1;
    if (esRespuestaOk(j) && j.contains("result") && j["result"].contains("id") &&
        j["result"]["id"].is_number_unsigned()) {
        id = j["result"]["id"].get<int>();
    }

    if (id >= 0) {
        j = ejecutar("objeto-transformar", solicitudJson("scene.object.settransform", {
            {"project", "Aislado"}, {"id", id},
            {"position", {1.0, 2.0, 3.0}}
        }), rc);
        CHECK(rc == 0, "scene.object.settransform: codigo de salida");
        CHECK(esRespuestaOk(j), "scene.object.settransform: respuesta satisfactoria");

        j = ejecutar("objeto-leer", solicitudJson("scene.object.get", {
            {"project", "Aislado"}, {"id", id}
        }), rc);
        CHECK(rc == 0, "scene.object.get: codigo de salida");
        CHECK(esRespuestaOk(j), "scene.object.get: respuesta satisfactoria");
        double posicionX = 0.0;
        if (esRespuestaOk(j) && j.contains("result") &&
            j["result"].contains("object") &&
            j["result"]["object"].contains("transform") &&
            j["result"]["object"]["transform"].contains("position") &&
            j["result"]["object"]["transform"]["position"].is_array() &&
            !j["result"]["object"]["transform"]["position"].empty()) {
            posicionX = j["result"]["object"]["transform"]["position"][0].get<double>();
        }
        CHECK(posicionX == 1.0,
              "scene.object.settransform: la posicion debe persistirse");
    }

    j = ejecutar("src-crear", solicitudJson("project.src.create", {
        {"project", "Aislado"}, {"path", "Directorio/Archivo.txt"},
        {"file", true}, {"content", "contenido"}
    }), rc);
    CHECK(rc == 0, "project.src.create: codigo de salida");
    CHECK(esRespuestaOk(j), "project.src.create: respuesta satisfactoria");

    j = ejecutar("src-listar", solicitudJson("project.src.list", {
        {"project", "Aislado"}, {"path", "Directorio"}
    }), rc);
    CHECK(rc == 0, "project.src.list: codigo de salida");
    CHECK(esRespuestaOk(j), "project.src.list: respuesta satisfactoria");

    j = ejecutar("src-escape", solicitudJson("project.src.create", {
        {"project", "Aislado"}, {"path", "../fuera.txt"}, {"file", true}
    }), rc);
    CHECK(rc == 4, "project.src.create con escape: codigo de salida");
    CHECK(codigoError(j) == "invalid_path",
          "project.src.create con escape: error invalid_path");

    j = ejecutar("tipo-invalido", solicitudJson("project.src.create", {
        {"project", "Aislado"}, {"path", "x"}, {"file", 123}
    }), rc);
    CHECK(rc == 2, "argumento con tipo invalido: codigo de salida");
    CHECK(codigoError(j) == "invalid_request",
          "argumento con tipo invalido: error invalid_request");
}

void probarAgente(const EntornoAislado& env) {
    auto ejecutar = [&](const std::string& nombre, const nlohmann::json& solicitud, int& rc) {
        return env.ejecutar(nombre, solicitud, rc);
    };
    const fs::path& trabajo = env.trabajo;
    const fs::path datos = trabajo / "datos";
    (void)datos;
    int rc = 0;
    nlohmann::json j;
    j = ejecutar("agente", solicitudJson("agente.procesar", {
        {"text", "/version"}
    }), rc);
    CHECK(rc == 0, "agente.procesar: codigo de salida");
    CHECK(esRespuestaOk(j), "agente.procesar: respuesta satisfactoria");
    if (esRespuestaOk(j) && j.contains("result") && j["result"].is_object()) {
        const auto& r = j["result"];
        CHECK(r.value("fueComandoCli", false),
              "agente.procesar: la orden se interpreto como comando CLI");
        CHECK(r.value("comandoOk", false),
              "agente.procesar: el comando interno se ejecuto");
        CHECK(r.contains("auditoria") && r["auditoria"].value("entradas", 0) == 1,
              "agente.procesar: Harness dejo una entrada de auditoria");
        CHECK(r.value("auditoria", nlohmann::json::object())
                  .value("ultimoComando", std::string()) == "version",
              "agente.procesar: la auditoria registra version");
    }
}

}  // namespace pruebas_cli
