/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: el chat con el agente — razonamiento plegado, lo que pasa
// mientras trabaja (aclaraciones incorporadas al bucle en curso, en cada
// formato, y la ronda extra si iba a terminar) y al editar el chat (descarte
// de pedidos con vuelta a la tarea anterior), y la aclaración en la tarea.
#include "PruebasAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/LLM/Bucle/ToolRegistry.h"
#include "Agente/Silvy.h"
#include "Nucleo/NucleoCLI.h"
#include "Tui/AplicacionTui.h"

namespace pruebas_agente {

namespace {

bool contiene(const std::string& s, const std::string& parte) { return s.find(parte) != std::string::npos; }

// Herramienta que, al ejecutarse, simula que el usuario manda una aclaración.
class HerramientaQueAclara : public llm::Tool {
public:
    explicit HerramientaQueAclara(std::shared_ptr<llm::BuzonInstrucciones> buzon) : buzon_(std::move(buzon)) {}
    llm::ToolSpec spec() const override { return {"crear", "Crea algo", R"({"type":"object","properties":{}})"}; }
    llm::ToolResult execute(const nlohmann::json&, const llm::ToolContext&) override {
        buzon_->agregar("que se llame Esfera");
        return llm::ToolResult::ok("creado Cubo");
    }

private:
    std::shared_ptr<llm::BuzonInstrucciones> buzon_;
};

// Un turno en modo prompt: la primera respuesta llama a "crear", las demás contestan.
std::vector<LlamadaVista> turnoConAclaracion(llm::FormatoAclaracion formato, llm::AgentAnswer& respuesta,
                                             std::vector<llm::ChatMessage>& historial) {
    auto registro = std::make_shared<GatewayGuion::Registro>();
    auto gateway = std::make_shared<GatewayGuion>(
        [](const LlamadaVista& v) {
            const bool primera = v.mensajes.back().content.find("crea un Cubo") != std::string::npos &&
                                 v.mensajes.back().role == "user" && v.mensajes.size() <= 2;
            return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText(
                primera ? "```tool_call\n{\"tool\":\"crear\",\"arguments\":{}}\n```" : "Listo.")};
        },
        registro);
    auto buzon = std::make_shared<llm::BuzonInstrucciones>();
    auto tools = std::make_shared<llm::ToolRegistry>();
    tools->add(std::make_shared<HerramientaQueAclara>(buzon));
    llm::AgentLoop bucle(gateway, tools, false, 1000);
    bucle.setBuzon(buzon);
    bucle.setFormatoAclaracion(formato);
    bucle.setDocumentacionAclaraciones([](const std::string& t) { return "DOC para «" + t + "»"; });
    historial = {llm::ChatMessage::user("crea un Cubo")};
    respuesta = bucle.run(historial, nullptr, "[documentación]");
    std::lock_guard<std::mutex> lock(registro->mutex);
    return registro->llamadas;
}

// Herramienta que solo cuenta sus llamadas.
class HerramientaContada : public llm::Tool {
public:
    explicit HerramientaContada(std::string nombre) : nombre_(std::move(nombre)) {}
    llm::ToolSpec spec() const override { return {nombre_, "prueba", R"({"type":"object","properties":{}})"}; }
    llm::ToolResult execute(const nlohmann::json&, const llm::ToolContext&) override {
        ++llamadas;
        return llm::ToolResult::ok("hecho");
    }
    int llamadas = 0;

private:
    std::string nombre_;
};

}  // namespace

void probarChat() {
    // 1. Razonamiento plegado.
    {
        funshi::chat::MensajeChat m;
        m.rol = funshi::chat::MensajeChat::Rol::Pensamiento;
        m.texto = "uno\ndos\ntres\ncuatro\ncinco";
        std::vector<funshi::chat::LineaChat> lineas;
        funshi::AplicacionTui::partirMensajeChat(m, 60, lineas);
        CHECK(lineas.size() == 3 && contiene(lineas[2].texto, "3 líneas más · clic para ver el razonamiento"),
              "razonamiento: plegado en dos líneas y el aviso");
        m.expandido = true;
        funshi::AplicacionTui::partirMensajeChat(m, 60, lineas);
        CHECK(lineas.size() == 6 && contiene(lineas.back().texto, "clic para plegar"), "razonamiento: desplegado completo");
        m.texto = "corto";
        m.expandido = false;
        funshi::AplicacionTui::partirMensajeChat(m, 60, lineas);
        CHECK(lineas.size() == 1, "razonamiento: uno corto no se pliega");
    }

    // 2. Aclaraciones durante el turno, en cada formato.
    using F = llm::FormatoAclaracion;
    for (const auto& [formato, nombre] : std::vector<std::pair<F, std::string>>{
             {F::MensajeNuevo, "mensaje nuevo"}, {F::EnResultados, "en resultados"},
             {F::EnPedido, "en el pedido"}, {F::EnSistema, "en el sistema"}}) {
        llm::AgentAnswer r;
        std::vector<llm::ChatMessage> historial;
        const auto llamadas = turnoConAclaracion(formato, r, historial);
        CHECK(llamadas.size() == 2, "aclaración " + nombre + ": una ronda después de la herramienta");
        std::string segunda;
        for (const auto& m : llamadas.size() > 1 ? llamadas[1].mensajes : std::vector<llm::ChatMessage>{}) {
            segunda += "<" + m.role + ">" + m.content + "\n";
        }
        CHECK(contiene(segunda, "que se llame Esfera"), "aclaración " + nombre + ": llega al modelo en la ronda siguiente");
        CHECK(r.aclaraciones == std::vector<std::string>{"que se llame Esfera"}, "aclaración " + nombre + ": queda en la respuesta");
        std::string guardado;
        for (const auto& m : historial) guardado += m.content + "\n";
        CHECK(contiene(segunda, "DOC para «que se llame Esfera»") && !contiene(guardado, "DOC para"),
              "aclaración " + nombre + ": su documentación llega en el turno y no queda en el historial");
        if (formato == F::EnPedido) {
            CHECK(historial.size() > 1 && contiene(historial[1].content, "Aclaración del usuario: que se llame Esfera") &&
                      !contiene(historial[1].content, "[documentación]"),
                  "aclaración en el pedido: el historial la guarda, sin el contexto del turno");
        }
        if (formato == F::EnSistema) {
            CHECK(llamadas.size() > 1 && contiene(llamadas[1].mensajes.front().content, "ACLARACIÓN DEL USUARIO"),
                  "aclaración en el sistema: va en las instrucciones");
        }
    }
    {
        // Llega cuando el bucle iba a terminar: hace una ronda más para atenderla.
        auto registro = std::make_shared<GatewayGuion::Registro>();
        auto buzon = std::make_shared<llm::BuzonInstrucciones>();
        auto gateway = std::make_shared<GatewayGuion>(
            [buzon](const LlamadaVista& v) {
                if (v.mensajes.size() <= 2) buzon->agregar("y que sea rojo");
                return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText("Listo.")};
            },
            registro);
        llm::AgentLoop bucle(gateway, std::make_shared<llm::ToolRegistry>(), false, 1000);
        bucle.setBuzon(buzon);
        std::vector<llm::ChatMessage> historial = {llm::ChatMessage::user("crea un Cubo")};
        bucle.run(historial);
        CHECK(registro->llamadas.size() == 2 && contiene(registro->llamadas[1].mensajes.back().content, "y que sea rojo"),
              "aclaración al final: una ronda más con ella");
    }

    // 3. Descartar pedidos: historial y tareas vuelven atrás.
    {
        funshi::SesionAgente s;
        s.historial = {llm::ChatMessage::system("sys"),
                       llm::ChatMessage::user("pedido 1"),
                       llm::ChatMessage::assistant("r1"),
                       llm::ChatMessage::user("pedido 2"),
                       llm::ChatMessage::assistant("```tool_call x```"),
                       llm::ChatMessage::user(std::string(llm::kInicioSalida) + " …] resultados"),
                       llm::ChatMessage::user("[Aclaración del usuario mientras trabajabas]\nmás"),
                       llm::ChatMessage::assistant("r2"),
                       llm::ChatMessage::user("pedido 3"),
                       llm::ChatMessage::assistant("r3")};
        funshi::agente::tareas::TareaAgente t1, t2, t3;
        t1.pedido = "pedido 1";
        t2.pedido = "pedido 2";
        t3.pedido = "pedido 3";
        s.tareasPrevias = {t1, t2};
        s.tareaAnterior = t3;
        auto copia = s;
        copia.descartarPedidos(1);
        CHECK(copia.historial.size() == 8 && copia.historial.back().content == "r2" && copia.tareaAnterior.pedido == "pedido 2",
              "descartar: el último pedido y su tarea");
        s.descartarPedidos(2);
        CHECK(s.historial.size() == 3 && s.historial.back().content == "r1" && s.tareaAnterior.pedido == "pedido 1" &&
                  s.tareasPrevias.empty(),
              "descartar: dos pedidos, con sus resultados y aclaraciones");
        s.descartarPedidos(5);
        CHECK(s.historial.size() == 1 && s.historial.front().role == "system" && s.tareaAnterior.vacia(),
              "descartar: más pedidos de los que hay deja solo el sistema");
    }

    // 6. Borrar pide confirmación al usuario: con un no no se borra, con un sí sí.
    {
        funshi::Solicitud alta;
        alta.comando = "project.create";
        alta.argumentos = {{"name", "Pborrar"}, {"dimension", "3d"}};
        funshi::ejecutarSolicitud(alta);
        funshi::Solicitud obj;
        obj.comando = "scene.object.create";
        obj.argumentos = {{"project", "Pborrar"}, {"name", "Barril"}};
        const int id = funshi::ejecutarSolicitud(obj).respuesta.resultado.value("id", -1);
        funshi::Auditoria auditoria("auditoria-borrar.log");
        funshi::Harness harness(auditoria);
        funshi::Silvy silvy(harness);
        auto registro = std::make_shared<GatewayGuion::Registro>();
        silvy.configurarConFabrica(
            [registro, id]() {
                return std::make_shared<GatewayGuion>(
                    [id](const LlamadaVista& v) -> std::vector<llm::LlmChunk> {
                        if (v.mensajes.back().role == "user") {
                            return {llm::LlmChunk::makeToolCall(
                                "c1", "ejecutar_comando",
                                nlohmann::json{{"comando", "scene.object.delete"},
                                               {"argumentos", {{"project", "Pborrar"}, {"id", id}}}}.dump())};
                        }
                        return {llm::LlmChunk::makeText("Listo.")};
                    },
                    registro);
            },
            true, 1);
        funshi::SesionAgente sesion;
        std::vector<std::string> preguntas;
        bool responder = false;
        sesion.confirmar = [&](const std::string& d) {
            preguntas.push_back(d);
            return responder;
        };
        const auto existe = [&] {
            funshi::Solicitud ver;
            ver.comando = "scene.object.get";
            ver.argumentos = {{"project", "Pborrar"}, {"id", id}};
            return funshi::ejecutarSolicitud(ver).respuesta.correcta;
        };
        silvy.procesar("borra el barril", sesion, nullptr);
        CHECK(preguntas.size() == 1 && contiene(preguntas[0], "borrar el objeto «Barril»") && existe(),
              "borrar: se pregunta con el nombre del objeto y con un no queda");
        responder = true;
        silvy.procesar("borra el barril", sesion, nullptr);
        CHECK(preguntas.size() == 2 && !existe(), "borrar: con un sí se borra");
    }

    // 5. Nombres mal escritos: se corrigen solo dentro del mismo comando.
    {
        auto crear = std::make_shared<HerramientaContada>("scene.object.create");
        auto etiqueta = std::make_shared<HerramientaContada>("scene.object.settag");
        auto meta = std::make_shared<HerramientaContada>("ejecutar_comando");
        auto tools = std::make_shared<llm::ToolRegistry>();
        tools->add(crear);
        tools->add(etiqueta);
        tools->add(meta);
        auto registro = std::make_shared<GatewayGuion::Registro>();
        auto gateway = std::make_shared<GatewayGuion>(
            [](const LlamadaVista& v) {
                const bool primera = v.mensajes.back().content == "prueba";
                return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText(
                    primera ? "```tool_call\n{\"tool\":\"scene.object.rename\",\"arguments\":{}}\n"
                              "{\"tool\":\"scene.object.setname\",\"arguments\":{}}\n"
                              "{\"tool\":\"scene.object.creat\",\"arguments\":{}}\n"
                              "{\"tool\":\"ejecutr_comand\",\"arguments\":{}}\n```"
                            : "Listo.\n[Aclaración del usuario mientras trabajabas]")};
            },
            registro);
        llm::AgentLoop bucle(gateway, tools, false, 1000);
        std::vector<llm::ChatMessage> historial = {llm::ChatMessage::user("prueba")};
        const auto r = bucle.run(historial);
        CHECK(crear->llamadas == 1 && etiqueta->llamadas == 0 && meta->llamadas == 1,
              "nombres: rename y setname no se convierten en create/settag; creat y ejecutr_comand sí se corrigen");
        CHECK(r.text.find("[Aclaración") == std::string::npos, "respuesta: sin la marca de aclaración copiada");
    }

    // 4. Silvy: la aclaración queda en la tarea cerrada.
    {
        funshi::Auditoria auditoria("auditoria-encurso.log");
        funshi::Harness harness(auditoria);
        funshi::Silvy silvy(harness);
        funshi::SesionAgente sesion;
        auto registro = std::make_shared<GatewayGuion::Registro>();
        auto buzon = sesion.aclaraciones;
        silvy.configurarConFabrica(
            [registro, buzon]() {
                return std::make_shared<GatewayGuion>(
                    [buzon](const LlamadaVista& v) -> std::vector<llm::LlmChunk> {
                        const std::string& u = v.mensajes.back().content;
                        if (contiene(u, "Escribe SOLO una línea")) return {llm::LlmChunk::makeText("Ahora: crear Cubo.")};
                        if (contiene(u, "Terminaste una tarea"))
                            return {llm::LlmChunk::makeText(contiene(u, "que sea rojo") ? "Lo que hice:\n- Cubo rojo\nResumen: con la aclaración."
                                                                                       : "Lo que hice:\n- Cubo\nResumen: sin aclaración.")};
                        if (v.mensajes.back().role == "user" && !contiene(u, "[Aclaración")) {
                            buzon->agregar("que sea rojo");
                            return {llm::LlmChunk::makeToolCall("c1", "ejecutar_comando", R"({"comando":"version"})")};
                        }
                        return {llm::LlmChunk::makeText("Listo.")};
                    },
                    registro);
            },
            true, 1);
        silvy.setTareasActivas(true);
        silvy.procesar("crea un Cubo", sesion, nullptr);
        CHECK(contiene(sesion.tareaAnterior.pedido, "Aclaración: que sea rojo") && sesion.tareaAnterior.resumen == "con la aclaración.",
              "silvy: la aclaración entra en la tarea y en su cierre");
    }
}

}  // namespace pruebas_agente
