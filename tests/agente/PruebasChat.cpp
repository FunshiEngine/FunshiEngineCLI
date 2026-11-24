/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: el chat con el agente — razonamiento plegado y lo que pasa
// mientras trabaja (aclaraciones incorporadas al bucle en curso, en cada
// formato, y la ronda extra si iba a terminar), y la aclaración en la tarea.
#include "PruebasAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/LLM/Bucle/ToolRegistry.h"
#include "Agente/Silvy.h"
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
    historial = {llm::ChatMessage::user("crea un Cubo")};
    respuesta = bucle.run(historial, nullptr, "[documentación]");
    std::lock_guard<std::mutex> lock(registro->mutex);
    return registro->llamadas;
}

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
