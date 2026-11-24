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

#include <nlohmann/json.hpp>

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
    CHECK(claves(esquemaDe(*reg, "project.src.create")).back() == "resumen", "esquemas: se conserva resumen");

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
