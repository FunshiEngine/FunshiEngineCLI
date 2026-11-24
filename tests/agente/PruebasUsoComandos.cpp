/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: la información con la que el modelo usa los comandos —
// esquemas generados de la ayuda, firmas con tipos, proyecto por defecto,
// errores con el uso correcto y guías convertidas en llamadas reales.
#include "PruebasAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Conocimiento/BaseConocimiento.h"
#include "Agente/Conocimiento/GuiasEjecutables.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/LLM/Herramientas/PerfilesHerramientas.h"
#include "Nucleo/AyudaComandos.h"
#include "Nucleo/NucleoCLI.h"

#include <Configuracion/ProjectPaths.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace pruebas_agente {

namespace {

nlohmann::ordered_json esquemaDe(const llm::ToolRegistry& reg, const std::string& nombre) {
    for (const auto& s : reg.specs()) {
        if (s.name == nombre) return nlohmann::ordered_json::parse(s.parametersSchema);
    }
    return {};
}

std::vector<std::string> claves(const nlohmann::ordered_json& esquema) {
    std::vector<std::string> v;
    for (const auto& [k, _] : esquema["properties"].items()) v.push_back(k);
    return v;
}

}  // namespace

void probarUsoDeComandos() {
    namespace con = funshi::agente::conocimiento;
    funshi::Auditoria auditoria("auditoria-comandos.log");
    auto harness = std::make_shared<funshi::Harness>(auditoria);
    auto reg = llm::crearRegistroPerfil(harness, "completo");

    // 1. Esquemas = ayuda del comando.
    const auto diag = esquemaDe(*reg, "diagnostics.list");
    CHECK(claves(diag) == (std::vector<std::string>{"limit"}) && !diag.contains("required"),
          "esquemas: diagnostics.list sin project y con limit");
    const auto crear = esquemaDe(*reg, "project.create");
    CHECK(crear["required"] == nlohmann::ordered_json::array({"name"}), "esquemas: dimension opcional en project.create");
    const auto obj = esquemaDe(*reg, "scene.object.create");
    CHECK(claves(obj) == (std::vector<std::string>{"project", "name", "parentId"}) &&
              obj["properties"]["parentId"]["type"] == "integer",
          "esquemas: project primero, tipos de la ayuda");
    bool todasDescritas = true;
    for (const auto& s : reg->specs()) {
        if (!funshi::buscarAyudaComando(s.name)) continue;
        for (const auto& [k, v] : nlohmann::ordered_json::parse(s.parametersSchema)["properties"].items()) {
            todasDescritas = todasDescritas && v.contains("description");
        }
    }
    CHECK(todasDescritas, "esquemas: cada argumento tiene descripción");
    {
        const auto c = claves(esquemaDe(*reg, "project.src.create"));
        CHECK(c.size() >= 2 && c[c.size() - 2] == "resumen" && c.back() == "sobrescribir",
              "esquemas: se conservan resumen y sobrescribir");
    }

    // 2. Firma con tipos en el prompt del modo sin tools nativas.
    {
        auto registro = std::make_shared<GatewayGuion::Registro>();
        auto gw = std::make_shared<GatewayGuion>(
            [](const LlamadaVista&) { return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText("ok")}; }, registro);
        llm::AgentLoop bucle(gw, reg, false);
        std::vector<llm::ChatMessage> h{llm::ChatMessage::user("hola")};
        bucle.run(h);
        const std::string sistema = registro->llamadas.at(0).mensajes.front().content;
        CHECK(sistema.find("scene.object.create(project*:texto, name*:texto, parentId:entero)") != std::string::npos,
              "firma: tipos y orden de la ayuda");
        CHECK(sistema.find("scene.object.settag(project*:texto, id*:entero, tag*:texto)") != std::string::npos,
              "firma: obligatorios marcados");
        CHECK(sistema.find("respondé") == std::string::npos && sistema.find("ejecutás") == std::string::npos,
              "prompt: sin voseo mezclado");
    }

    // 3. Proyecto por defecto: se completa en los comandos que lo usan.
    funshi::Solicitud alta;
    alta.comando = "project.create";
    alta.argumentos = {{"name", "Pdef"}, {"dimension", "3d"}};
    CHECK(funshi::ejecutarSolicitud(alta).respuesta.correcta, "proyecto por defecto: se crea el proyecto de prueba");
    harness->setProyectoPorDefecto("Pdef");
    CHECK(harness->ejecutarTexto("scene.list").respuesta.correcta, "proyecto por defecto: scene.list sin project");
    CHECK(harness->ejecutarTexto("project.list").respuesta.correcta, "proyecto por defecto: no toca comandos sin proyecto");
    CHECK(!harness->ejecutarTexto("scene.list", {{"project", "NoExiste"}}).respuesta.correcta,
          "proyecto por defecto: no pisa el project explícito");
    harness->setProyectoPorDefecto("");

    // 3b. "project" con la ruta o con otras mayúsculas: se corrige al nombre.
    harness->setProyectosConocidos({"Pdef"});
    CHECK(harness->ejecutarTexto("scene.list", {{"project", "/datos/FunshiEngine-DATA/Proyects/Pdef/srcPdef"}})
              .respuesta.correcta,
          "proyecto reconocido: ruta de src del proyecto");
    CHECK(harness->ejecutarTexto("scene.list", {{"project", "C:\\datos\\Proyects\\Pdef"}}).respuesta.correcta,
          "proyecto reconocido: ruta de Windows");
    CHECK(harness->ejecutarTexto("scene.list", {{"project", "pdef"}}).respuesta.correcta,
          "proyecto reconocido: otras mayúsculas");
    CHECK(funshi::Harness::proyectoReconocido("srcPdef", {"Pdef"}).empty(),
          "proyecto reconocido: un nombre suelto no se toma como carpeta src");
    CHECK(funshi::Harness::proyectoReconocido("/a/Otro/b", {"Pdef"}).empty(),
          "proyecto reconocido: ruta sin proyecto conocido");
    CHECK(!harness->ejecutarTexto("scene.list", {{"project", "NoExiste"}}).respuesta.correcta,
          "proyecto reconocido: un nombre desconocido sigue fallando");
    harness->setProyectosConocidos({});

    // 4. Error de argumentos: el resultado enseña el uso correcto.
    {
        llm::ToolContext ctx;
        const auto tool = reg->byName("ejecutar_comando");
        const auto r = tool->execute({{"comando", "scene.object.settag"}, {"argumentos", {{"project", "Pdef"}}}}, ctx);
        const std::string texto = r.render();
        CHECK(!r.ok() && texto.find("Uso correcto de scene.object.settag") != std::string::npos &&
                  texto.find("Ejemplo: {\"comando\":\"scene.object.settag\",\"argumentos\":{\"project\":") != std::string::npos,
              "errores: argumentos y ejemplo del comando (" + texto.substr(0, 120) + ")");
    }

    // Consulta al RAG sin la mención del proyecto existente.
    CHECK(funshi::Silvy::consultaParaConocimiento("crea un objeto llamado Cubo en el proyecto Demo", {"Demo"}).find("proyecto") == std::string::npos,
          "rag: sin «en el proyecto X» de un proyecto existente");
    CHECK(funshi::Silvy::consultaParaConocimiento("crea el proyecto Nuevo", {"Demo"}) == "crea el proyecto Nuevo",
          "rag: un proyecto que no existe sí queda (pedido de crear)");
    {
        const auto r = con::BaseConocimiento::global()->buscar(
            funshi::Silvy::consultaParaConocimiento("crea un objeto llamado Cubo en el proyecto Demo", {"Demo"}), 1);
        CHECK(!r.empty() && r.front().documento->id == "scene.object.create", "rag: el objeto encabeza la búsqueda");
    }
    // Llamada envuelta y "arguments" en lugar de "argumentos".
    {
        const auto sc = llm::ToolCallSanitizer::block(
            "```json\n{\"tool_call\":{\"tool\":\"ejecutar_comando\",\"arguments\":{\"comando\":\"scene.list\",\"arguments\":{\"project\":\"Pdef\"}}}}\n```");
        CHECK(sc.ok() && sc.name == "ejecutar_comando", "sanitizador: llamada envuelta en tool_call");
        llm::ToolContext ctx;
        CHECK(sc.ok() && reg->byName("ejecutar_comando")->execute(sc.args, ctx).ok(), "ejecutar_comando: acepta \"arguments\"");
    }

    // 4b. Formatos de llamada de modelos chicos.
    {
        // Los argumentos de ejecutar_comando escritos como la llamada, y sin la última llave.
        const auto sc = llm::ToolCallSanitizer::block(
            "```tool_call\n{\"comando\":\"scene.list\",\"argumentos\":{\"project\":\"Pdef\"}\n```");
        CHECK(sc.ok() && sc.name == "ejecutar_comando" && sc.args.value("comando", "") == "scene.list" &&
                  sc.args["argumentos"].value("project", "") == "Pdef",
              "sanitizador: {\"comando\"…} sin tool → ejecutar_comando");
        // Dos llamadas en un bloque, una por línea, la primera sin cerrar.
        const auto partes = llm::PromptToolParser::llamadasSinCerco(
            "```json\n{\"tool\":\"ejecutar_comando\",\"arguments\":{\"comando\":\"a\",\"argumentos\":{\"x\":1}}\n"
            "{\"tool\":\"ejecutar_comando\",\"arguments\":{\"comando\":\"b\",\"argumentos\":{}}}\n```");
        CHECK(partes.size() == 2 && llm::ToolCallSanitizer::block(partes[0]).ok() &&
                  llm::ToolCallSanitizer::block(partes[0]).args.value("comando", "") == "a" &&
                  llm::ToolCallSanitizer::block(partes[1]).args.value("comando", "") == "b",
              "parser: varias llamadas por línea en un bloque");
        // Un JSON con objetos en líneas propias (lista de tareas) no se parte.
        const auto lista = llm::PromptToolParser::llamadasSinCerco(
            "{\"tool\":\"lanzar_subagentes\",\"arguments\":{\"tareas\":[\n{\"titulo\":\"a\"},\n{\"titulo\":\"b\"}]}}");
        CHECK(lista.size() == 1, "parser: objetos de una lista no se toman como llamadas");
        CHECK(llm::sinMarcasDeSalida("Creé el Cubo.\n\n[FIN DE LA SALIDA]\n") == "Creé el Cubo." &&
                  llm::sinMarcasDeSalida("[SALIDA DE TUS HERRAMIENTAS — x]\nListo") == "Listo",
              "respuesta: sin las marcas copiadas del mensaje de resultados");
        CHECK(llm::sinMarcasDeSalida("Creé Enemigo.java.\n\nRESULTADO DE scene.object.create:\n{\"id\":3}\n\nListo.") ==
                  "Creé Enemigo.java.\n\n\nListo.",
              "respuesta: sin el resultado copiado");
    }

    // 4c. Protección de archivos: no se reemplaza uno existente sin permiso.
    {
        harness->setCorreccionesDelAgente(true);
        const nlohmann::json crear = {{"project", "Pdef"}, {"path", "Datos/notas.txt"}, {"file", true}, {"content", "original"}};
        CHECK(harness->ejecutarTexto("project.src.create", crear).respuesta.correcta, "protección: crea un archivo nuevo");
        auto pisar = crear;
        pisar["content"] = "otro";
        const auto r = harness->ejecutarTexto("project.src.create", pisar);
        CHECK(!r.respuesta.correcta && r.respuesta.codigoError == "file_exists" &&
                  r.respuesta.mensajeError.find("sobrescribir") != std::string::npos,
              "protección: rechaza reemplazar sin sobrescribir");
        const auto leer = [] {
            std::ifstream f(std::filesystem::path(ProjectPaths::directorioSrc("Pdef")) / "Datos/notas.txt");
            return std::string(std::istreambuf_iterator<char>(f), {});
        };
        CHECK(leer().find("original") != std::string::npos, "protección: el archivo queda como estaba");
        auto agregar = crear;
        agregar["content"] = "original\n  nuevo método";
        CHECK(harness->ejecutarTexto("project.src.create", agregar).respuesta.correcta &&
                  leer().find("nuevo método") != std::string::npos,
              "protección: conservar todas las líneas y agregar no necesita sobrescribir");
        pisar["sobrescribir"] = true;
        CHECK(harness->ejecutarTexto("project.src.create", pisar).respuesta.correcta && leer().find("otro") != std::string::npos,
              "protección: con sobrescribir lo reemplaza");
        const auto malScript = harness->ejecutarTexto(
            "project.src.create", {{"project", "Pdef"}, {"path", "Scripts/Bola.java"}, {"file", true},
                                   {"content", "using UnityEngine; public class Bola : MonoBehaviour { void Update() {} }"}});
        CHECK(!malScript.respuesta.correcta && malScript.respuesta.codigoError == "invalid_script" &&
                  malScript.respuesta.mensajeError.find("public class Bola implements Comportamiento") != std::string::npos,
              "scripts: el agente no escribe un script de otro motor; el error trae la plantilla del motor");
        // "src/" delante: se quita (si no, crearía src/src/...).
        const auto conSrc = harness->ejecutarTexto(
            "project.src.create", {{"project", "Pdef"}, {"path", "src/Scripts/C.java"}, {"file", true}, {"content", "class C {}"}});
        CHECK(conSrc.respuesta.correcta && conSrc.respuesta.resultado.value("path", "") == "Scripts/C.java",
              "correcciones: ruta sin \"src/\" delante");
        const auto sinExt = harness->ejecutarTexto(
            "project.src.create", {{"project", "Pdef"}, {"path", "Scripts/Enemigo"}, {"file", true}, {"content", "class E {}"}});
        CHECK(!sinExt.respuesta.correcta && sinExt.respuesta.codigoError == "missing_extension" &&
                  sinExt.respuesta.mensajeError.find("\"Scripts/Enemigo.java\"") != std::string::npos,
              "correcciones: archivo con código sin extensión, con la ruta sugerida");
        const auto conNombre = harness->ejecutarTexto(
            "project.src.create",
            {{"project", "Pdef"}, {"path", "Scripts/"}, {"name", "E.java"}, {"file", true}, {"content", "class E {}"}});
        CHECK(conNombre.respuesta.correcta && conNombre.respuesta.resultado.value("path", "") == "Scripts/E.java",
              "correcciones: name + carpeta → ruta del archivo");
        CHECK(harness->ejecutarTexto("project.src.create",
                                     {{"project", "Pdef"}, {"path", "Scripts/D.java"}, {"file", true}, {"content", nullptr}})
                  .respuesta.correcta,
              "correcciones: un argumento en null cuenta como ausente");
        harness->setProyectoPorDefecto("Pdef");
        CHECK(harness->ejecutarTexto("scene.list", {{"project", "<proyecto>"}}).respuesta.correcta,
              "correcciones: el marcador <proyecto> usa el proyecto de trabajo");
        harness->setProyectoPorDefecto("");
        harness->setCorreccionesDelAgente(false);
        pisar["content"] = "sin protección";
        pisar.erase("sobrescribir");
        CHECK(harness->ejecutarTexto("project.src.create", pisar).respuesta.correcta,
              "protección: desactivada, el comando se comporta como siempre");
    }

    // 5. Guías convertidas en llamadas y documentación de sus comandos.
    std::vector<std::string> comandos;
    const std::string g = con::guiaEjecutable(
        "1. scene.object.create {\"project\":P,\"name\":\"X\"}\n2. scene.object.addcomponent {\"project\":P,\"id\":ID,\"componentType\":\"RigidBody\"}.",
        {"scene.object.create"}, &comandos);
    CHECK(g.find("1. {\"tool\":\"scene.object.create\",\"arguments\":{\"project\":\"<proyecto>\",\"name\":\"X\"}}") != std::string::npos,
          "guías: comando con herramienta propia (" + g + ")");
    CHECK(g.find("{\"tool\":\"ejecutar_comando\",\"arguments\":{\"comando\":\"scene.object.addcomponent\",\"argumentos\":{\"project\":\"<proyecto>\",\"id\":<id>,") != std::string::npos,
          "guías: comando sin herramienta, vía ejecutar_comando, con marcadores");
    CHECK(comandos == (std::vector<std::string>{"scene.object.create", "scene.object.addcomponent"}), "guías: comandos nombrados");
    std::vector<std::string> usados;
    const auto ctxFisica = con::BaseConocimiento::global()->contextoParaPedido(
        "crea un objeto llamado Cubo en la escena y ponle física", {"scene.object.create"}, 4, &usados);
    CHECK(std::find(usados.begin(), usados.end(), "scene.object.addcomponent") != usados.end() &&
              ctxFisica.find("## scene.object.addcomponent") != std::string::npos,
          "guías: la documentación del comando de la guía entra al contexto");
}

}  // namespace pruebas_agente
