/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: monitoreo desde el chat — razonamiento separado del texto,
// eventos de progreso y transcripción que arma la TUI (orden de los
// mensajes, subagentes en su propia vista y persistencia).
#include "PruebasAgente.h"

#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/LLM/Bucle/SeparadorPensamiento.h"
#include "Tui/Chat/MonitorChat.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace pruebas_agente {

namespace {

using Tipo = llm::AgentEvent::Type;
using Rol = funshi::chat::MensajeChat::Rol;

void probarSeparador() {
    llm::SeparadorPensamiento sep;
    std::string texto, pensamiento;
    // Etiquetas partidas entre fragmentos, como llegan del stream.
    for (const char* f : {"Hola <th", "ink>debo usar ", "una herramienta</thi", "nk> listo", " <"}) {
        const auto p = sep.agregar(f);
        texto += p.texto;
        pensamiento += p.pensamiento;
    }
    const auto resto = sep.terminar();
    texto += resto.texto;
    CHECK(pensamiento == "debo usar una herramienta", "separador: razonamiento entre <think> (" + pensamiento + ")");
    CHECK(texto == "Hola  listo <", "separador: el texto queda sin etiquetas (" + texto + ")");
}

// El bucle emite el razonamiento como Pensamiento (fuera del historial) y
// notas de progreso por ronda.
void probarEventosBucle() {
    auto registro = std::make_shared<GatewayGuion::Registro>();
    auto gateway = std::make_shared<GatewayGuion>(
        [](const LlamadaVista&) {
            return std::vector<llm::LlmChunk>{llm::LlmChunk::makeReasoning("pienso en el pedido"),
                                              llm::LlmChunk::makeText("<think>y en la respuesta</think>Hola")};
        },
        registro);
    llm::AgentLoop bucle(gateway, std::make_shared<llm::ToolRegistry>(), true);
    std::vector<llm::ChatMessage> historial{llm::ChatMessage::user("hola")};
    std::vector<llm::AgentEvent> eventos;
    const auto r = bucle.run(historial, [&](const llm::AgentEvent& e) { eventos.push_back(e); });
    std::string pensado;
    int estados = 0;
    for (const auto& e : eventos) {
        if (e.type == Tipo::Pensamiento) pensado += e.delta;
        if (e.type == Tipo::Estado && e.callId == "ronda-1") ++estados;
    }
    CHECK(r.text == "Hola", "bucle: la respuesta no incluye el razonamiento (" + r.text + ")");
    CHECK(pensado == "pienso en el pedidoy en la respuesta",
          "bucle: razonamiento del proveedor y de <think> como Pensamiento (" + pensado + ")");
    CHECK(historial.back().content == "Hola",
          "bucle: el historial guarda solo la respuesta (" + historial.back().role + ": " + historial.back().content + ")");
    CHECK(estados == 2, "bucle: nota de ronda al empezar y al terminar (misma id)");
}

llm::AgentEvent ev(Tipo t, std::string delta = "", std::string origen = "") {
    llm::AgentEvent e{t, std::move(delta)};
    e.origen = std::move(origen);
    return e;
}

llm::AgentEvent herramienta(Tipo t, const std::string& nombre, const std::string& args, bool ok = true,
                            const std::string& resultado = "", const std::string& origen = "") {
    llm::AgentEvent e{t, "", nombre, nombre, args, ok, resultado};
    e.origen = origen;
    return e;
}

void probarTranscripcion() {
    funshi::chat::SesionTui sesion;
    funshi::chat::MonitorSesion monitor(sesion);
    monitor.esperar();
    monitor.aplicar(llm::AgentEvent::estado("ronda 1 · esperando al modelo", "ronda-1"));
    monitor.aplicar(ev(Tipo::Pensamiento, "Necesito mover el cubo."));
    monitor.aplicar(ev(Tipo::Text, "Voy a moverlo.\n```tool_call\n{\"tool\":\"ejecutar_comando\"}\n```"));
    monitor.aplicar(llm::AgentEvent::estado("ronda 1 · 2.0 s", "ronda-1"));
    const std::string args = R"({"comando":"scene.object.settransform","argumentos":{"project":"P","id":3}})";
    monitor.aplicar(herramienta(Tipo::ToolStart, "ejecutar_comando", args));
    monitor.aplicar(herramienta(Tipo::ToolEnd, "ejecutar_comando", "", true, R"({"ok":true,"id":3})"));

    // Subagente: su trabajo va a su vista; en la sesión queda un resumen.
    llm::AgentEvent inicio{Tipo::SubagenteInicio};
    inicio.name = "Revisar escena";
    inicio.origen = "sa-1";
    inicio.argsJson = R"({"titulo":"Revisar escena","instrucciones":"Lista los objetos","contexto":"project=P","perfil":"lectura","indice":1,"total":1})";
    monitor.aplicar(inicio);
    monitor.aplicar(ev(Tipo::Text, "mirando", "sa-1"));
    monitor.aplicar(herramienta(Tipo::ToolStart, "scene_objects", R"({"project":"P"})", true, "", "sa-1"));
    monitor.aplicar(herramienta(Tipo::ToolEnd, "scene_objects", "", true, "[]", "sa-1"));
    llm::AgentEvent fin{Tipo::SubagenteFin};
    fin.origen = "sa-1";
    fin.ok = true;
    fin.result = "RESULTADO: completado\nHECHO: 0 objetos";
    monitor.aplicar(fin);

    monitor.aplicar(ev(Tipo::Text, "Listo: el cubo está en su lugar."));
    monitor.cerrar("texto acumulado que no debe usarse", false);

    const auto& m = sesion.mensajes;
    std::vector<Rol> roles;
    for (const auto& x : m) roles.push_back(x.rol);
    const std::vector<Rol> esperado = {Rol::Estado, Rol::Pensamiento, Rol::Pensamiento, Rol::Herramienta,
                                       Rol::Subagente, Rol::Silvy};
    CHECK(roles == esperado, "monitor: orden nota → razonamiento → herramienta → subagente → respuesta");
    if (roles != esperado) return;
    CHECK(m[0].texto == "ronda 1 · 2.0 s", "monitor: la nota de ronda se actualiza en su lugar");
    CHECK(m[2].texto == "Voy a moverlo.", "monitor: el texto previo a la llamada queda como razonamiento");
    CHECK(m[3].texto.rfind("✔ scene.object.settransform", 0) == 0 && m[3].ok,
          "monitor: la herramienta muestra el comando y su estado (" + m[3].texto + ")");
    CHECK(m[3].detalle.find("\"id\": 3") != std::string::npos && m[3].detalle.find("resultado:") != std::string::npos,
          "monitor: el detalle tiene argumentos y resultado completos");
    CHECK(m[4].ref == "sa-1" && m[4].texto.rfind("✔ 1/1 Revisar escena · completado · 1 acción", 0) == 0,
          "monitor: resumen del subagente (" + m[4].texto + ")");
    CHECK(m[5].texto == "Listo: el cubo está en su lugar." && !m[5].enCurso,
          "monitor: la respuesta final es la del último turno");

    const auto sub = sesion.subagente("sa-1");
    CHECK(sub && sub->mensajes.size() == 4, "monitor: el subagente tiene su propia transcripción");
    if (sub && sub->mensajes.size() == 4) {
        CHECK(sub->mensajes[0].rol == Rol::Tarea && sub->mensajes[0].texto.find("Lista los objetos") != std::string::npos &&
                  sub->mensajes[0].texto.find("project=P") != std::string::npos,
              "monitor: la vista del subagente empieza con su tarea");
        // Lo que escribió antes de llamar la herramienta pasa a razonamiento.
        CHECK(sub->mensajes[1].rol == Rol::Pensamiento && sub->mensajes[1].texto == "mirando" &&
                  sub->mensajes[2].rol == Rol::Herramienta && sub->mensajes[3].rol == Rol::Silvy &&
                  sub->mensajes[3].texto.find("RESULTADO: completado") != std::string::npos,
              "monitor: el subagente muestra su trabajo y su informe");
        CHECK(sub->datos.estado == "completado" && !sub->enCurso, "monitor: estado final del subagente");
    }

    // Persistencia: la vista del subagente sobrevive a guardar y cargar.
    sesion.datos = funshi::agente::SesionChat::nueva("Prueba");
    sesion.volcarEnDatos();
    funshi::chat::SesionTui cargada;
    cargada.datos = funshi::agente::SesionChat::desdeJson(sesion.datos.aJson());
    cargada.cargarDesdeDatos();
    const auto subCargado = cargada.subagente("sa-1");
    CHECK(cargada.mensajes.size() == m.size() && cargada.mensajes[4].ref == "sa-1" &&
              cargada.mensajes[3].detalle == m[3].detalle,
          "persistencia: mensajes con detalle y referencia");
    CHECK(subCargado && subCargado->mensajes.size() == 4 && subCargado->datos.instrucciones == "Lista los objetos" &&
              subCargado->numero == 1,
          "persistencia: subagente con su tarea y su transcripción");
}

void probarCortes() {
    // Una herramienta que no terminó (cancelación) queda marcada.
    funshi::chat::SesionTui sesion;
    funshi::chat::MonitorSesion monitor(sesion);
    monitor.esperar();
    monitor.aplicar(herramienta(Tipo::ToolStart, "buscar_comandos", R"({"consulta":"mover"})"));
    monitor.cerrar("razonamiento ya mostrado\n[Cancelado]", false);
    CHECK(sesion.mensajes.size() == 2 && sesion.mensajes[0].texto.find("(interrumpida)") != std::string::npos &&
              !sesion.mensajes[0].ok,
          "monitor: herramienta interrumpida");
    CHECK(sesion.mensajes.size() == 2 && sesion.mensajes[1].texto.find("razonamiento ya mostrado") == std::string::npos,
          "monitor: sin respuesta en vivo no se repite lo ya mostrado");

    // Informe "parcial": el resumen lo dice (no "completado").
    {
        funshi::chat::SesionTui s;
        funshi::chat::MonitorSesion mon(s);
        llm::AgentEvent ini{Tipo::SubagenteInicio};
        ini.origen = "sa-p";
        ini.argsJson = R"({"titulo":"T","instrucciones":"x","perfil":"lectura","indice":1,"total":1})";
        mon.aplicar(ini);
        llm::AgentEvent fin{Tipo::SubagenteFin};
        fin.origen = "sa-p";
        fin.ok = true;
        fin.result = "RESULTADO: parcial\nPROBLEMAS: faltó una ruta";
        mon.aplicar(fin);
        CHECK(s.subagente("sa-p") && s.subagente("sa-p")->datos.estado == "parcial" &&
                  s.mensajes.back().texto.rfind("◐ 1/1 T · parcial", 0) == 0,
              "monitor: subagente con informe parcial (" + s.mensajes.back().texto + ")");
    }
    CHECK(funshi::chat::quitarBloquesLlamada("Antes\n\n```tool_call\n{}\n```\n\n\nDespués") == "Antes\n\nDespués",
          "monitor: sin renglones vacíos de más donde estaba la llamada");

    // Una llamada mal formada que no se ejecutó queda visible tal cual.
    funshi::chat::SesionTui mal;
    funshi::chat::MonitorSesion mmal(mal);
    mmal.esperar();
    mmal.aplicar(ev(Tipo::Text, "{\"tool\":\"x\",\"arguments\":{}}"));
    mmal.cerrar("", false);
    CHECK(mal.mensajes.size() == 1 && mal.mensajes[0].texto == "{\"tool\":\"x\",\"arguments\":{}}",
          "monitor: una llamada no ejecutada se muestra");

    // Comando CLI directo: sin eventos, el texto final es la respuesta.
    funshi::chat::SesionTui cli;
    funshi::chat::MonitorSesion mcli(cli);
    mcli.esperar();
    mcli.cerrar("Comando ejecutado: /version", false);
    CHECK(cli.mensajes.size() == 1 && cli.mensajes[0].texto == "Comando ejecutado: /version",
          "monitor: respuesta de un comando CLI");

    CHECK(funshi::chat::quitarBloquesLlamada("{\"tool\":\"x\",\"arguments\":{}}").empty(),
          "monitor: una llamada JSON suelta no es razonamiento");
    CHECK(funshi::chat::quitarBloquesLlamada("tool_call\n{\"comando\":\"version\"}").empty(),
          "monitor: llamada con etiqueta tool_call sin cerco");
    CHECK(funshi::chat::quitarBloquesLlamada("Mira:\n```json\n{\"a\":1}\n```") == "Mira:\n```json\n{\"a\":1}\n```",
          "monitor: un JSON explicativo se conserva");
    // Código escrito por una herramienta: bloque propio, completo y legible.
    {
        const std::string args =
            R"({"comando":"project.src.create","argumentos":{"project":"P","path":"src/Jugador.cs","file":true,"content":"using System;\n\tclass Jugador {}\n","resumen":"Clase Jugador vacía"}})";
        const std::string b = funshi::chat::bloqueDeCodigo("ejecutar_comando", args);
        CHECK(b == "┌ Jugador.cs · C# (.cs) · 2 líneas\n│ en: src/Jugador.cs\n│ resumen: Clase Jugador vacía\n"
                   "│ aviso: el motor no ejecuta scripts .cs (solo .java y .cpp)\n├─\n"
                   "│ using System;\n│     class Jugador {}\n└",
              "monitor: cabecera con nombre, lenguaje, resumen y aviso; tabs expandidos (" + b + ")");
        const std::string java = funshi::chat::bloqueDeCodigo(
            "project.src.create", R"({"project":"P","path":"Jugador.java","content":"class Jugador {}"})");
        CHECK(java.rfind("┌ Jugador.java · Java (.java) · Script Java del motor · 1 línea\n│ resumen: (el agente no lo indicó)\n├─", 0) == 0,
              "monitor: script del motor sin resumen (" + java + ")");
        const std::string en = funshi::chat::bloqueDeCodigo(
            "project.src.create", R"({"project":"P","path":"a.cpp","content":"x","summary":"Prueba"})");
        CHECK(en.find("│ resumen: Prueba\n") != std::string::npos, "monitor: acepta summary como resumen");
        const std::string d = funshi::chat::describirLlamada("ejecutar_comando", args);
        CHECK(d.find("using System") == std::string::npos && d.find("…2 líneas") != std::string::npos &&
                  d.find("resumen") == std::string::npos,
              "monitor: la línea de la acción no repite el código (" + d + ")");
        std::string largo;
        for (int i = 0; i < 70; ++i) largo += "x\n";
        std::string completo;
        const std::string bl = funshi::chat::bloqueDeCodigo(
            "file.create", nlohmann::json{{"name", "a.txt"}, {"content", largo}}.dump(), &completo);
        CHECK(bl.find("10 líneas más (clic para ver todo)") != std::string::npos &&
                  std::count(completo.begin(), completo.end(), '\n') == 70,
              "monitor: código largo recortado, con el completo para expandir");
        funshi::chat::SesionTui s;
        funshi::chat::MonitorSesion mon(s);
        mon.aplicar(herramienta(Tipo::ToolStart, "ejecutar_comando", args));
        CHECK(s.mensajes.size() == 2 && s.mensajes[1].rol == Rol::Codigo,
              "monitor: el código aparece debajo de la acción");
    }
    CHECK(llm::resultadosParaModelo("RESULTADO DE x:\nok\n").find("no lo escribió el usuario") != std::string::npos,
          "bucle: los resultados se presentan como salida de las herramientas del asistente");

    CHECK(funshi::chat::describirLlamada("buscar_comandos", R"({"consulta":"mover objeto"})") ==
              "buscar_comandos «mover objeto»",
          "monitor: descripción de buscar_comandos");
}

}  // namespace

void probarMonitor() {
    probarSeparador();
    probarEventosBucle();
    probarTranscripcion();
    probarCortes();
}

}  // namespace pruebas_agente
