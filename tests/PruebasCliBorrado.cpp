/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// cli-tests: borrado de objetos (con hijos, deshacer y rehacer), scripts y
// assets (quitando las referencias de los objetos), y listado de assets.
#include "PruebasCli.h"

#include <fstream>

namespace pruebas_cli {

namespace {

// Ids de la escena (recorrido de los "children" de scene.objects).
void juntarIds(const nlohmann::json& nodos, std::vector<std::pair<int, std::string>>& ids) {
    for (const auto& n : nodos) {
        ids.emplace_back(n.value("id", -1), n.value("name", std::string()));
        juntarIds(n.value("children", nlohmann::json::array()), ids);
    }
}

bool tieneComponente(const nlohmann::json& respuesta, const std::string& tipo) {
    for (const auto& c : respuesta["result"].value("components", nlohmann::json::array())) {
        if (c.dump().find(tipo) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

void probarBorrado(const EntornoAislado& env) {
    int rc = 0;
    const auto ejecutar = [&](const std::string& nombre, const std::string& comando, nlohmann::json args) {
        args["project"] = "Borrado";
        return env.ejecutar(nombre, solicitudJson(comando, args), rc);
    };
    const auto escena = [&] {
        std::vector<std::pair<int, std::string>> ids;
        const auto j = ejecutar("borrado-objetos", "scene.objects", nlohmann::json::object());
        juntarIds(j["result"].value("root", nlohmann::json::array()), ids);
        return ids;
    };
    const auto hay = [](const std::vector<std::pair<int, std::string>>& ids, const std::string& nombre) {
        for (const auto& [id, n] : ids) {
            if (n == nombre) return id;
        }
        return -1;
    };

    env.ejecutar("borrado-proyecto", solicitudJson("project.create", {{"name", "Borrado"}, {"dimension", "3d"}}), rc);
    auto j = ejecutar("borrado-padre", "scene.object.create", {{"name", "Padre"}});
    const int padre = j["result"].value("id", -1);
    j = ejecutar("borrado-hijo", "scene.object.create", {{"name", "Hijo"}, {"parentId", padre}});
    const int hijo = j["result"].value("id", -1);
    CHECK(padre > 0 && hijo > 0, "borrado: objetos de prueba creados");

    // Un script y un sonido, cada uno usado por un objeto.
    ejecutar("borrado-script", "project.src.create",
             {{"path", "Scripts/Jugador.java"}, {"file", true},
              {"content", "public class Jugador implements Comportamiento {\n"
                          "    public void iniciar(long objeto) {}\n"
                          "    public void actualizar(long objeto, double deltaTime) {}\n}\n"}});
    const fs::path wav = env.tmp / "salto.wav";
    std::ofstream(wav) << "RIFF";
    j = ejecutar("borrado-importar", "asset.import", {{"source", wav.string()}});
    CHECK(esRespuestaOk(j) && j["result"].value("path", "") == "Sonidos/salto.wav", "asset.import: sonido de prueba");
    ejecutar("borrado-usa-script", "scene.object.addcomponent",
             {{"id", hijo}, {"componentType", "Script"}, {"value", "Scripts/Jugador.java"}});
    ejecutar("borrado-usa-sonido", "scene.object.addcomponent",
             {{"id", padre}, {"componentType", "AudioSource"}, {"value", "Sonidos/salto.wav"}});

    // asset.list: tipos y filtro.
    j = ejecutar("borrado-assets", "asset.list", nlohmann::json::object());
    CHECK(esRespuestaOk(j) && j["result"].value("count", 0) == 1 && j["result"]["assets"][0].value("type", "") == "Audio",
          "asset.list: el sonido con su tipo y sin los scripts");
    j = ejecutar("borrado-assets-modelo", "asset.list", {{"type", "modelo"}});
    CHECK(esRespuestaOk(j) && j["result"].value("count", -1) == 0, "asset.list: filtro por tipo");

    // Errores: tipo equivocado, archivo inexistente, ruta fuera de src/.
    j = ejecutar("borrado-no-script", "script.delete", {{"path", "Sonidos/salto.wav"}});
    CHECK(codigoError(j) == "not_a_script", "script.delete: un asset no es un script");
    j = ejecutar("borrado-no-asset", "asset.delete", {{"path", "Scripts/Jugador.java"}});
    CHECK(codigoError(j) == "not_an_asset", "asset.delete: un script no es un asset");
    j = ejecutar("borrado-no-existe", "asset.delete", {{"path", "Sonidos/nada.wav"}});
    CHECK(codigoError(j) == "file_not_found", "asset.delete: archivo inexistente");
    j = ejecutar("borrado-escapa", "asset.delete", {{"path", "../../fuera.wav"}});
    CHECK(!esRespuestaOk(j), "asset.delete: no sale de src/");

    // script.delete y asset.delete quitan los componentes que los usaban.
    j = ejecutar("borrado-script-delete", "script.delete", {{"path", "Scripts/Jugador.java"}});
    CHECK(esRespuestaOk(j) && j["result"]["referencesRemoved"].size() == 1 &&
              j["result"]["referencesRemoved"][0].value("id", -1) == hijo,
          "script.delete: borra y quita el Script del objeto que lo usaba");
    CHECK(!tieneComponente(ejecutar("borrado-comp-hijo", "scene.object.components", {{"id", hijo}}), "Script"),
          "script.delete: el objeto ya no tiene el Script");
    j = ejecutar("borrado-asset-delete", "asset.delete", {{"path", "Sonidos/salto.wav"}});
    CHECK(esRespuestaOk(j) && j["result"]["referencesRemoved"].size() == 1 &&
              j["result"]["referencesRemoved"][0].value("componentType", "").find("Audio") != std::string::npos,
          "asset.delete: borra y quita el AudioSource que lo usaba");
    j = ejecutar("borrado-assets-vacio", "asset.list", nlohmann::json::object());
    CHECK(esRespuestaOk(j) && j["result"].value("count", -1) == 0, "asset.delete: el archivo ya no está");

    // scene.object.delete: con sus hijos, deshacer y rehacer.
    j = ejecutar("borrado-raiz", "scene.object.delete", {{"id", 0}});
    CHECK(codigoError(j) == "invalid_request", "scene.object.delete: la raíz no se borra");
    j = ejecutar("borrado-objeto", "scene.object.delete", {{"id", padre}});
    CHECK(esRespuestaOk(j) && j["result"]["deletedIds"].size() == 2 && j["result"].value("undoable", false),
          "scene.object.delete: borra el objeto y su hijo");
    auto ids = escena();
    CHECK(hay(ids, "Padre") < 0 && hay(ids, "Hijo") < 0, "scene.object.delete: ya no están en la escena");
    j = ejecutar("borrado-undo", "scene.undo", nlohmann::json::object());
    ids = escena();
    const int restaurado = hay(ids, "Padre");
    CHECK(esRespuestaOk(j) && j["result"].value("undone", false) && restaurado > 0 && hay(ids, "Hijo") > 0 &&
              j["result"].value("id", -1) == restaurado,
          "scene.undo: vuelve el objeto con su hijo");
    j = ejecutar("borrado-redo", "scene.redo", nlohmann::json::object());
    ids = escena();
    CHECK(esRespuestaOk(j) && j["result"].value("redone", false) && hay(ids, "Padre") < 0,
          "scene.redo: lo borra otra vez");
}

}  // namespace pruebas_cli
