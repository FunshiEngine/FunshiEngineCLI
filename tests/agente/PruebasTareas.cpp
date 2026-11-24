/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: tareas del agente — formato del contexto, lectura del
// objetivo y del cierre que escribe el modelo, respaldo y el recorrido
// completo en Silvy (objetivo antes del bucle, cierre después, contexto de
// la tarea siguiente y persistencia).
#include "PruebasAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Sesiones/SesionChat.h"
#include "Agente/Silvy.h"
#include "Agente/Tareas/TareaAgente.h"
#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/LLM/Bucle/ToolRegistry.h"
#include "Nucleo/NucleoCLI.h"

#include <algorithm>

namespace pruebas_agente {

namespace tareas = funshi::agente::tareas;

namespace {

bool contiene(const std::string& s, const std::string& parte) { return s.find(parte) != std::string::npos; }

// Mensaje de sistema de la llamada cuyo pedido contiene `marca`.
std::string l0(const std::vector<LlamadaVista>& llamadas, const std::string& marca) {
    for (const auto& l : llamadas) {
        if (contiene(l.mensajes.back().content, marca) && !l.mensajes.empty()) return l.mensajes.front().content;
    }
    return {};
}

}  // namespace

void probarTareas() {
    // 1. Formato y lectura.
    tareas::TareaAgente vacia;
    CHECK(tareas::bloqueTareaAnterior(vacia).empty(), "tareas: sin tarea anterior no hay bloque");
    tareas::TareaAgente t;
    t.pedido = "crea un objeto Cubo";
    t.hecho = {"Creé Cubo (id 1)"};
    t.resumen = "Había que crear Cubo; quedó con id 1.";
    CHECK(tareas::bloqueTareaAnterior(t) ==
              "## Tarea anterior\nLo que hice:\n- Creé Cubo (id 1)\nResumen: Había que crear Cubo; quedó con id 1.\n",
          "tareas: bloque de la tarea anterior en markdown");
    const std::string pObj = tareas::pedidoDeObjetivo(t, "ahora ponle física");
    CHECK(contiene(pObj, "## Tarea anterior") && contiene(pObj, "## Tarea actual\nahora ponle física") &&
              contiene(pObj, "\"Ahora:"),
          "tareas: pedido del objetivo con la tarea anterior y la actual");
    CHECK(tareas::leerObjetivo("Ahora: agregar un RigidBody al Cubo (id 1).") == "agregar un RigidBody al Cubo (id 1).",
          "objetivo: línea Ahora");
    CHECK(tareas::leerObjetivo("Claro.\n**Ahora:** \"borrar Scripts/B.cpp\"") == "borrar Scripts/B.cpp",
          "objetivo: con negritas y comillas");
    CHECK(tareas::leerObjetivo("Voy a borrar el archivo").empty(), "objetivo: sin la línea, vacío");

    tareas::TareaAgente c;
    CHECK(tareas::leerCierre("Lo que hice:\n- Creé Cubo (id 1)\n- Agregué RigidBody\nResumen: Cubo con física.", c) &&
              c.hecho.size() == 2 && c.hecho[1] == "Agregué RigidBody" && c.resumen == "Cubo con física." &&
              c.resumenDelModelo,
          "cierre: lista y resumen");
    tareas::TareaAgente c2;
    CHECK(tareas::leerCierre("* Renombré A.cpp a B.cpp\n**Resumen:** archivo renombrado", c2) && c2.hecho.size() == 1 &&
              c2.resumen == "archivo renombrado",
          "cierre: sin encabezado y con viñetas *");
    tareas::TareaAgente c3;
    CHECK(!tareas::leerCierre("<lista>- x</lista>", c3), "cierre: sin Resumen no se acepta");

    std::vector<funshi::agente::llm::AgentAnswer::ExecutedTool> acciones = {
        {"c1", "scene.object.create", R"({"name":"Cubo"})", true, R"({"id":1})", {}},
        {"c2", "scene.object.addcomponent", R"({"id":9})", false, "Objeto no encontrado", {}}};
    CHECK(contiene(tareas::pedidoDeCierre("crea Cubo", acciones, "Listo"), "OK scene.object.create") &&
              contiene(tareas::pedidoDeCierre("crea Cubo", acciones, "Listo"), "FALLÓ scene.object.addcomponent"),
          "cierre: el pedido lleva las herramientas con su resultado");
    tareas::TareaAgente r;
    r.pedido = "crea Cubo";
    tareas::cierreDeRespaldo(acciones, "Listo", r);
    CHECK(r.hecho.size() == 2 && contiene(r.hecho[1], "falló: Objeto no encontrado") && !r.resumenDelModelo &&
              contiene(r.resumen, "crea Cubo"),
          "cierre: respaldo con las herramientas ejecutadas");
    const auto ida = tareas::TareaAgente::desdeJson(t.aJson());
    CHECK(ida.pedido == t.pedido && ida.hecho == t.hecho && ida.resumen == t.resumen, "tareas: ida y vuelta por JSON");

    // 2. Recorrido en Silvy con un modelo guionado.
    funshi::Solicitud alta;
    alta.comando = "project.create";
    alta.argumentos = {{"name", "Ptareas"}, {"dimension", "3d"}};
    funshi::ejecutarSolicitud(alta);
    funshi::Auditoria auditoria("auditoria-tareas.log");
    funshi::Harness harness(auditoria);
    funshi::Silvy silvy(harness);
    auto registro = std::make_shared<GatewayGuion::Registro>();
    bool cierreValido = true;
    silvy.configurarConFabrica(
        [registro, &cierreValido]() {
            return std::make_shared<GatewayGuion>(
                [&cierreValido](const LlamadaVista& v) -> std::vector<llm::LlmChunk> {
                    const std::string& u = v.mensajes.back().content;
                    if (contiene(u, "Escribe SOLO una línea")) {
                        return {llm::LlmChunk::makeText(contiene(u.substr(u.rfind("## Tarea actual")), "física") ? "Ahora: agregar física al Cubo (id 1)."
                                                                                : "Ahora: crear el objeto Cubo.")};
                    }
                    if (contiene(u, "Terminaste una tarea")) {
                        return {llm::LlmChunk::makeText(cierreValido ? "Lo que hice:\n- Creé Cubo (id 1)\nResumen: Había que crear "
                                                                       "Cubo; quedó con id 1."
                                                                     : "no sé")};
                    }
                    if (v.mensajes.back().role == "user") {
                        const bool falla = contiene(u, "comando que no existe");
                        return {llm::LlmChunk::makeToolCall(
                            "c1", "ejecutar_comando",
                            falla ? R"({"comando":"no.existe","argumentos":{}})"
                                  : R"({"comando":"scene.list","argumentos":{"project":"Ptareas"}})")};
                    }
                    return {llm::LlmChunk::makeText("Listo.")};
                },
                registro);
        },
        true, 1);
    silvy.setTareasActivas(true);
    funshi::SesionAgente sesion;
    sesion.proyectoActivo = "Ptareas";
    std::vector<std::string> notas;
    const auto anotar = [&](const llm::AgentEvent& e) {
        if (e.type == llm::AgentEvent::Type::Estado) notas.push_back(e.delta);
    };
    silvy.procesar("crea un objeto Cubo", sesion, anotar);
    const auto principal = [&](std::size_t desde) -> std::string {
        std::lock_guard<std::mutex> lock(registro->mutex);
        for (std::size_t i = desde; i < registro->llamadas.size(); ++i) {
            const auto& u = registro->llamadas[i].mensajes.back();
            if (u.role == "user" && contiene(u.content, "## Tarea actual") && !contiene(u.content, "Escribe SOLO")) return u.content;
        }
        return {};
    };
    const std::string turno1 = principal(0);
    CHECK(contiene(turno1, "## Tarea actual\ncrea un objeto Cubo\nAhora: crear el objeto Cubo.") &&
              !contiene(turno1, "## Tarea anterior"),
          "silvy: el turno lleva la tarea actual y el objetivo del modelo");
    CHECK(sesion.tareaAnterior.pedido == "crea un objeto Cubo" && sesion.tareaAnterior.objetivo == "crear el objeto Cubo." &&
              sesion.tareaAnterior.hecho == std::vector<std::string>{"Creé Cubo (id 1)"} && sesion.tareaAnterior.resumenDelModelo,
          "silvy: la tarea queda cerrada con lo que escribió el modelo");
    {
        std::lock_guard<std::mutex> lock(registro->mutex);
        std::string pedidoObjetivo;
        for (const auto& l : registro->llamadas) {
            if (contiene(l.mensajes.back().content, "Escribe SOLO una línea")) pedidoObjetivo = l.mensajes.back().content;
        }
        CHECK(contiene(pedidoObjetivo, "[Documentación de referencia") &&
                  contiene(l0(registro->llamadas, "Escribe SOLO una línea"), "FunshiEngine"),
              "silvy: el objetivo se pide con la documentación del turno y el contexto del motor");
    }
    CHECK(sesion.historial.size() > 1 && sesion.historial[1].content == "crea un objeto Cubo",
          "silvy: el historial guarda el pedido sin el contexto del turno");
    const bool notaObjetivo = std::find(notas.begin(), notas.end(), "Tarea: crear el objeto Cubo.") != notas.end();
    const bool notaResumen =
        std::find(notas.begin(), notas.end(), "Resumen de la tarea: Había que crear Cubo; quedó con id 1.") != notas.end();
    CHECK(notaObjetivo && notaResumen, "silvy: el chat recibe el objetivo y el resumen");

    std::size_t desde;
    {
        std::lock_guard<std::mutex> lock(registro->mutex);
        desde = registro->llamadas.size();
    }
    cierreValido = false;
    silvy.procesar("ahora ponle física", sesion, nullptr);
    const std::string turno2 = principal(desde);
    const auto posAnterior = turno2.find("## Tarea anterior\nLo que hice:\n- Creé Cubo (id 1)");
    CHECK(posAnterior != std::string::npos && posAnterior < turno2.find("## Tarea actual\nahora ponle física") &&
              contiene(turno2, "Ahora: agregar física al Cubo (id 1)."),
          "silvy: la tarea siguiente recibe la anterior y su propio objetivo");
    CHECK(!sesion.tareaAnterior.resumenDelModelo && sesion.tareaAnterior.hecho.size() == 1 &&
              contiene(sesion.tareaAnterior.hecho[0], "ejecutar_comando"),
          "silvy: cierre sin formato → respaldo con las herramientas");

    // Si ninguna herramienta salió bien, el cierre no se le pide al modelo
    // (contaría lo que no hizo): se arma con lo que pasó.
    cierreValido = true;
    silvy.procesar("usa un comando que no existe", sesion, nullptr);
    CHECK(!sesion.tareaAnterior.resumenDelModelo && contiene(sesion.tareaAnterior.hecho.front(), "falló"),
          "silvy: sin herramientas exitosas, cierre con lo que pasó");

    // Herramienta inventada para responder: es la respuesta final.
    CHECK(llm::detalle::respuestaDisfrazada("fin de la respuesta", {{"argumentos", {{"respuesta", "Creé Cubo."}}}}) ==
              "Creé Cubo.",
          "respuesta disfrazada: herramienta de fin con un texto");
    CHECK(llm::detalle::respuestaDisfrazada("Respuesta al usuario", {{"texto", "a"}, {"otro", "b"}}).empty() &&
              llm::detalle::respuestaDisfrazada("scene.crear", {{"texto", "x"}}).empty(),
          "respuesta disfrazada: no con varios textos ni con otros nombres");
    {
        auto regFin = std::make_shared<GatewayGuion::Registro>();
        auto gFin = std::make_shared<GatewayGuion>(
            [](const LlamadaVista&) {
                return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText(
                    "```tool_call\n{\"tool\":\"fin de la respuesta\",\"arguments\":{\"respuesta\":\"Creé Cubo.\"}}\n```")};
            },
            regFin);
        llm::AgentLoop bucleFin(gFin, std::make_shared<llm::ToolRegistry>(), false, 1000);
        std::vector<llm::ChatMessage> msgs = {llm::ChatMessage::user("crea Cubo")};
        const auto rFin = bucleFin.run(msgs);
        CHECK(rFin.text == "Creé Cubo." && rFin.toolCalls.empty() && regFin->llamadas.size() == 1,
              "bucle: la herramienta inventada de fin termina el turno con su texto (" + rFin.text + ")");
    }

    funshi::agente::SesionChat chat = funshi::agente::SesionChat::nueva("tareas");
    chat.agente = sesion;
    const auto vuelta = funshi::agente::SesionChat::desdeJson(chat.aJson());
    CHECK(vuelta.agente.tareaAnterior.pedido == "usa un comando que no existe", "sesiones: la tarea anterior se guarda");
}

}  // namespace pruebas_agente
