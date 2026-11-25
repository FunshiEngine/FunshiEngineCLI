/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// cli-tests: borrado de objetos (con hijos, deshacer y rehacer), scripts y
// assets (quitando las referencias de los objetos), listado de assets y la
// papelera (recuperar con referencias, conflictos, objetos, vaciar).
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

    // Papelera: lo borrado se lista y se recupera con sus referencias.
    j = ejecutar("papelera-lista", "trash.list", nlohmann::json::object());
    CHECK(esRespuestaOk(j) && j["result"].value("count", 0) == 2, "trash.list: el script y el sonido borrados");
    int idScript = -1, idSonido = -1;
    for (const auto& item : j["result"].value("items", nlohmann::json::array())) {
        if (item.value("path", "") == "Scripts/Jugador.java") idScript = item.value("id", -1);
        if (item.value("path", "") == "Sonidos/salto.wav") idSonido = item.value("id", -1);
    }
    j = ejecutar("papelera-restaura-script", "trash.restore", {{"id", idScript}});
    CHECK(esRespuestaOk(j) && j["result"]["referencesRestored"].size() == 1 &&
              tieneComponente(ejecutar("papelera-comp-hijo", "scene.object.components", {{"id", hijo}}), "Script"),
          "trash.restore: vuelve el script y su componente en el objeto");
    j = ejecutar("papelera-restaura-dos-veces", "trash.restore", {{"id", idScript}});
    CHECK(codigoError(j) == "not_in_trash", "trash.restore: lo restaurado ya no está en la papelera");
    // Conflicto: ya hay otro archivo con esa ruta.
    std::ofstream(wav) << "RIFF2";
    ejecutar("papelera-otro-sonido", "asset.import", {{"source", wav.string()}});
    j = ejecutar("papelera-conflicto", "trash.restore", {{"id", idSonido}});
    CHECK(codigoError(j) == "destination_exists", "trash.restore: no pisa un archivo que ya existe");
    j = ejecutar("papelera-reemplaza", "trash.restore", {{"id", idSonido}, {"overwrite", true}});
    CHECK(esRespuestaOk(j) && j["result"]["referencesRestored"].size() == 1 &&
              tieneComponente(ejecutar("papelera-comp-padre", "scene.object.components", {{"id", padre}}), "Audio"),
          "trash.restore: con overwrite reemplaza y vuelve el AudioSource");

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

    // scene.objects.delete: borra varios/todos con una sola operación.
    ejecutar("borrado-undo-para-grupo", "scene.undo", nlohmann::json::object()); // restaura Padre/Hijo
    auto crear = [&](const std::string& nombre, int padreId) {
        return ejecutar("borrado-crear-" + nombre, "scene.object.create", {{"name", nombre}, {"parentId", padreId}})
            ["result"]["id"].get<int>();
    };
    const int a = crear("GrupoA", 0);
    const int b = crear("GrupoB", 0);
    crear("HijoB", b);
    j = ejecutar("borrado-varios", "scene.objects.delete", {{"ids", nlohmann::json::array({a, b, 9999})}});
    CHECK(esRespuestaOk(j) && j["result"].value("count", 0) == 2 && j["result"]["skipped"].size() == 1,
          "scene.objects.delete: borra grupo, omite inexistente");
    ids = escena();
    CHECK(hay(ids, "GrupoA") < 0 && hay(ids, "GrupoB") < 0 && hay(ids, "HijoB") < 0,
          "scene.objects.delete: grupo eliminado");
    // Borrar Padre/Hijo para dejar escena limpia antes de all=true.
    int padreActual = hay(ids, "Padre");
    if (padreActual > 0) ejecutar("borrado-limpia-padre", "scene.object.delete", {{"id", padreActual}});
    crear("Todo1", 0);
    crear("Todo2", 0);
    j = ejecutar("borrado-todos", "scene.objects.delete", {{"all", true}});
    CHECK(esRespuestaOk(j) && j["result"].value("count", 0) >= 2, "scene.objects.delete: all=true");
    ids = escena();
    CHECK(ids.empty(), "scene.objects.delete: solo queda la raíz");

    // Restablecer estado esperado por las comprobaciones de papelera posteriores.
    ejecutar("borrado-reset-papelera", "trash.empty", nlohmann::json::object());
    const int padreR = ejecutar("borrado-reset-padre", "scene.object.create", {{"name", "Padre"}, {"parentId", 0}})["result"].value("id", -1);
    ejecutar("borrado-reset-hijo", "scene.object.create", {{"name", "Hijo"}, {"parentId", padreR}});
    ejecutar("borrado-reset-delete", "scene.object.delete", {{"id", padreR}});

    // Un objeto borrado también se recupera desde la papelera.
    j = ejecutar("papelera-objetos", "trash.list", nlohmann::json::object());
    int idObjeto = -1;
    for (const auto& item : j["result"].value("items", nlohmann::json::array())) {
        if (item.value("name", "") == "Padre") idObjeto = item.value("id", -1);
    }
    CHECK(idObjeto > 0, "trash.list: hay entrada de objeto Padre borrado");
    j = ejecutar("papelera-restaura-objeto", "trash.restore", {{"id", idObjeto}});
    ids = escena();
    CHECK(esRespuestaOk(j) && hay(ids, "Padre") > 0 && hay(ids, "Hijo") > 0, "trash.restore: vuelve el objeto con su hijo");
    j = ejecutar("papelera-undo-ya-no", "scene.undo", nlohmann::json::object());
    CHECK(!j["result"].value("undone", false) || j["result"].value("type", "") != "delete",
          "trash.restore: el borrado sale del historial (scene.undo no lo duplica)");

    // trash.empty: definitivo.
    ejecutar("papelera-borra-otra-vez", "asset.delete", {{"path", "Sonidos/salto.wav"}});
    j = ejecutar("papelera-vaciar", "trash.empty", nlohmann::json::object());
    CHECK(esRespuestaOk(j) && j["result"].value("emptied", 0) == 1, "trash.empty: vacía la papelera");
    j = ejecutar("papelera-vacia", "trash.list", nlohmann::json::object());
    CHECK(esRespuestaOk(j) && j["result"].value("count", -1) == 0, "trash.empty: no queda nada");
}

}  // namespace pruebas_cli
