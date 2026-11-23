/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: Silvy con un modelo falso — RAG inyectado en el turno,
// delegación en subagentes y validación de las tareas.
#include "PruebasAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"
#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/Subagentes/HerramientaSubagentes.h"

#include <algorithm>

namespace pruebas_agente {

namespace {

bool esSubagente(const LlamadaVista& v) {
    return !v.mensajes.empty() && v.mensajes.front().content.find("Eres un subagente") != std::string::npos;
}

bool hayResultadoHerramienta(const LlamadaVista& v) {
    return std::any_of(v.mensajes.begin(), v.mensajes.end(),
                       [](const llm::ChatMessage& m) { return m.role == "tool"; });
}

// Principal: delega dos tareas y, con los informes, responde "Listo".
// Subagente: ejecuta `version` y devuelve su informe.
std::vector<llm::LlmChunk> guionDelegacion(const LlamadaVista& v) {
    if (esSubagente(v)) {
        if (!hayResultadoHerramienta(v)) {
            return {llm::LlmChunk::makeToolCall("c1", "ejecutar_comando", R"({"comando":"version"})")};
        }
        return {llm::LlmChunk::makeText("RESULTADO: completado\nHECHO: consulté la versión\nPROBLEMAS: ninguno")};
    }
    if (!hayResultadoHerramienta(v)) {
        return {llm::LlmChunk::makeToolCall(
            "d1", "lanzar_subagentes",
            R"({"tareas":[)"
            R"({"titulo":"Versión A","instrucciones":"Consulta la versión de la CLI e informa el número","contexto":"sin proyecto","perfil":"lectura"},)"
            R"({"titulo":"Versión B","instrucciones":"Consulta la versión de la CLI e informa el número","contexto":"sin proyecto","perfil":"lectura"}]})")};
    }
    return {llm::LlmChunk::makeText("Listo")};
}

}  // namespace

void probarSubagentes() {
    funshi::Auditoria auditoria("auditoria-subagentes.log");
    funshi::Harness harness(auditoria);
    funshi::Silvy silvy(harness);
    auto registro = std::make_shared<GatewayGuion::Registro>();
    silvy.configurarConFabrica([registro]() { return std::make_shared<GatewayGuion>(guionDelegacion, registro); },
                               /*nativas=*/true, /*paralelo=*/2);

    funshi::SesionAgente sesion;
    sesion.proyectoActivo = "MiJuego";
    std::vector<llm::AgentEvent> eventos;
    std::mutex mutexEventos;
    const std::string pedido = "revisa la versión dos veces por separado";
    const auto resp = silvy.procesar(pedido, sesion, [&](const llm::AgentEvent& e) {
        std::lock_guard<std::mutex> lock(mutexEventos);
        eventos.push_back(e);
    });

    CHECK(resp.texto == "Listo", "delegación: respuesta final del principal (" + resp.texto + ")");
    using Tipo = llm::AgentEvent::Type;
    // Id del subagente con ese título (por su evento de inicio).
    const auto idDe = [&](const std::string& titulo) {
        for (const auto& e : eventos) {
            if (e.type == Tipo::SubagenteInicio && e.name == titulo) return e.origen;
        }
        return std::string();
    };
    const auto cuenta = [&](const std::string& origen, Tipo tipo) {
        return std::count_if(eventos.begin(), eventos.end(),
                             [&](const llm::AgentEvent& e) { return e.origen == origen && e.type == tipo; });
    };
    const std::string idA = idDe("Versión A"), idB = idDe("Versión B");
    CHECK(!idA.empty() && !idB.empty() && idA != idB, "delegación: cada subagente tiene su id");
    CHECK(cuenta(idA, Tipo::SubagenteFin) == 1 && cuenta(idB, Tipo::SubagenteFin) == 1,
          "delegación: fin de cada subagente");
    CHECK(cuenta(idA, Tipo::ToolStart) == 1 && cuenta(idA, Tipo::ToolEnd) == 1,
          "delegación: las herramientas del subagente salen con su origen");
    CHECK(cuenta(idA, Tipo::Estado) >= 1, "delegación: el subagente informa su progreso");
    const auto inicioA = std::find_if(eventos.begin(), eventos.end(), [&](const llm::AgentEvent& e) {
        return e.type == Tipo::SubagenteInicio && e.origen == idA;
    });
    const auto tarea = nlohmann::json::parse(inicioA != eventos.end() ? inicioA->argsJson : "{}", nullptr, false);
    CHECK(tarea.is_object() && tarea.value("perfil", "") == "lectura" &&
              tarea.value("instrucciones", "").find("versión de la CLI") != std::string::npos &&
              tarea.value("total", 0) == 2,
          "delegación: el inicio lleva la tarea completa");
    const auto finA = std::find_if(eventos.begin(), eventos.end(), [&](const llm::AgentEvent& e) {
        return e.type == Tipo::SubagenteFin && e.origen == idA;
    });
    CHECK(finA != eventos.end() && finA->ok && finA->result.find("RESULTADO: completado") != std::string::npos,
          "delegación: el fin lleva el informe");
    CHECK(cuenta("", Tipo::Estado) >= 2, "delegación: el principal informa documentación y rondas");

    std::vector<LlamadaVista> llamadas;
    {
        std::lock_guard<std::mutex> lock(registro->mutex);
        llamadas = registro->llamadas;
    }
    CHECK(llamadas.size() == 6, "delegación: 2 llamadas del principal + 2 por subagente (" +
                                    std::to_string(llamadas.size()) + ")");
    bool subSinDelegar = true, subLectura = true, principalConRag = false, informesAlPrincipal = false;
    for (const auto& v : llamadas) {
        const bool delega = std::count(v.herramientas.begin(), v.herramientas.end(), "lanzar_subagentes") > 0;
        if (esSubagente(v)) {
            subSinDelegar = subSinDelegar && !delega;
            subLectura = subLectura && std::count(v.herramientas.begin(), v.herramientas.end(),
                                                  "scene.object.create") == 0;
            continue;
        }
        CHECK(delega, "principal: tiene lanzar_subagentes");
        CHECK(v.mensajes.front().content.find("Proyecto activo del usuario: \"MiJuego\"") != std::string::npos,
              "principal: el prompt incluye el proyecto activo");
        for (const auto& m : v.mensajes) {
            if (m.role == "user" && m.content.find("[Documentación recuperada") != std::string::npos) {
                principalConRag = true;
            }
            if (m.role == "tool" && m.content.find("Informes de 2 subagente(s)") != std::string::npos &&
                m.content.find("RESULTADO: completado") != std::string::npos) {
                informesAlPrincipal = true;
            }
        }
    }
    CHECK(subSinDelegar, "subagentes: no pueden volver a delegar");
    CHECK(subLectura, "subagentes: perfil lectura sin herramientas de escritura");
    CHECK(principalConRag, "principal: recibe la documentación recuperada en el turno");
    CHECK(informesAlPrincipal, "principal: recibe los informes de los subagentes");

    // El contexto del turno no queda en el historial.
    bool pedidoLimpio = false;
    for (const auto& m : sesion.historial) {
        if (m.role == "user") pedidoLimpio = m.content == pedido;
    }
    CHECK(pedidoLimpio, "historial: el pedido queda sin la documentación inyectada");

    // Modo prompt (modelos sin tools nativas): llamada escrita sin el cerco
    // ```tool_call; se acepta solo si nombra una herramienta real.
    {
        funshi::Silvy prompt(harness);
        auto reg = std::make_shared<GatewayGuion::Registro>();
        prompt.configurarConFabrica(
            [reg]() {
                return std::make_shared<GatewayGuion>(
                    [](const LlamadaVista& v) {
                        const bool conResultado = v.mensajes.back().content.find("RESULTADO DE") != std::string::npos;
                        return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText(
                            conResultado ? "Versión consultada"
                                         : "Uso esto: {\"tool\":\"ejecutar_comando\",\"arguments\":{\"comando\":\"version\"}} "
                                           "y no esto: {\"tool\":\"inventada\",\"arguments\":{}}")};
                    },
                    reg);
            },
            /*nativas=*/false, 1);
        funshi::SesionAgente s;
        std::vector<std::string> usadas;
        const auto r = prompt.procesar("dime la versión", s, [&](const llm::AgentEvent& e) {
            if (e.type == llm::AgentEvent::Type::ToolStart) usadas.push_back(e.name);
        });
        CHECK(usadas == std::vector<std::string>{"ejecutar_comando"}, "modo prompt: llamada sin cerco ejecutada");
        // Solo los argumentos (sin nombre): se infiere ejecutar_comando por "comando".
        usadas.clear();
        funshi::Silvy solo(harness);
        solo.configurarConFabrica(
            [reg]() {
                return std::make_shared<GatewayGuion>(
                    [](const LlamadaVista& v) {
                        const bool conResultado = v.mensajes.back().content.find("RESULTADO DE") != std::string::npos;
                        return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText(
                            conResultado ? "ok" : "{\n  \"comando\": \"version\"\n}")};
                    },
                    reg);
            },
            false, 1);
        funshi::SesionAgente s2;
        solo.procesar("dime la versión", s2, [&](const llm::AgentEvent& e) {
            if (e.type == llm::AgentEvent::Type::ToolStart) usadas.push_back(e.name);
        });
        CHECK(usadas == std::vector<std::string>{"ejecutar_comando"}, "modo prompt: argumentos sin nombre → herramienta inferida");
        // Un comando del catálogo pedido como herramienta → vía ejecutar_comando.
        {
            std::vector<std::string> fin;
            funshi::Silvy alias(harness);
            alias.configurarConFabrica(
                [reg]() {
                    return std::make_shared<GatewayGuion>(
                        [](const LlamadaVista& v) {
                            const bool conResultado = v.mensajes.back().content.find("RESULTADO DE") != std::string::npos;
                            return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText(
                                conResultado ? "ok" : "```tool_call\n{\"tool\":\"version\",\"arguments\":{}}\n```\n"
                                                      "```tool_call\n{\"tool\":\"ejecutr_comand\",\"arguments\":{\"comando\":\"version\"}}\n```")};
                        },
                        reg);
                },
                false, 1);
            funshi::SesionAgente sa;
            alias.procesar("versión", sa, [&](const llm::AgentEvent& e) {
                if (e.type == llm::AgentEvent::Type::ToolEnd) fin.push_back(e.ok ? "ok" : e.result);
            });
            CHECK(fin == (std::vector<std::string>{"ok", "ok"}),
                  "alias: comando del catálogo y nombre mal escrito se ejecutan");
        }

        // La misma herramienta falla 3 veces: el turno se corta (no agota rondas).
        {
            auto regCorte = std::make_shared<GatewayGuion::Registro>();
            funshi::Silvy terco(harness);
            terco.configurarConFabrica(
                [regCorte]() {
                    return std::make_shared<GatewayGuion>(
                        [](const LlamadaVista&) {
                            return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText(
                                "```tool_call\n{\"tool\":\"lanzar_subagentes\",\"arguments\":{}}\n```")};
                        },
                        regCorte);
                },
                false, 1);
            funshi::SesionAgente st;
            const auto rt = terco.procesar("delega algo", st, nullptr);
            CHECK(rt.texto.find("falló 3 veces") != std::string::npos && regCorte->llamadas.size() == 3,
                  "corte: 3 fallos de la misma herramienta detienen el turno (" +
                      std::to_string(regCorte->llamadas.size()) + " llamadas)");
        }

        // ```json con la clave tool_call: también es una llamada.
        usadas.clear();
        funshi::Silvy cercoJson(harness);
        cercoJson.configurarConFabrica(
            [reg]() {
                return std::make_shared<GatewayGuion>(
                    [](const LlamadaVista& v) {
                        const bool conResultado = v.mensajes.back().content.find("RESULTADO DE") != std::string::npos;
                        return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText(
                            conResultado ? "El objeto es {\"project\":\"P\",\"name\":\"X\"}"
                                         : "```json\n{\n  \"tool_call\": \"ejecutar_comando\",\n  \"arguments\": {\"comando\": \"version\"}\n}\n```")};
                    },
                    reg);
            },
            false, 1);
        funshi::SesionAgente s3;
        const auto r3 = cercoJson.procesar("dime la versión", s3, [&](const llm::AgentEvent& e) {
            if (e.type == llm::AgentEvent::Type::ToolStart) usadas.push_back(e.name);
        });
        CHECK(usadas == std::vector<std::string>{"ejecutar_comando"}, "modo prompt: ```json con tool_call ejecutado");
        // La respuesta final contiene un JSON explicativo: no dispara otra llamada.
        CHECK(r3.texto.find("El objeto es") != std::string::npos, "modo prompt: JSON explicativo no es una llamada");
        CHECK(r.texto.find("Versión consultada") != std::string::npos, "modo prompt: respuesta tras el resultado");
    }

    // Bloque real de Phi-4-mini: clave "tool_call" y falta la última llave.
    {
        const auto sc = llm::ToolCallSanitizer::block(
            "```tool_call\n{\"tool_call\":\"lanzar_subagentes\",\"arguments\":{\"tareas\":[{\"titulo\":\"A\","
            "\"instrucciones\":\"Crea el objeto A e informa su id\",\"contexto\":\"proyecto P\",\"perfil\":\"escena\"}]}\n```");
        CHECK(sc.ok() && sc.name == "lanzar_subagentes" && sc.args["tareas"].size() == 1,
              "sanitizador: repara la llave final y acepta la clave tool_call");
        // Arreglo cerrado con llave (también real): "[{..},{..}}}}".
        const auto sc2 = llm::ToolCallSanitizer::block(
            "{\"tool\":\"lanzar_subagentes\",\"arguments\":{\"tareas\":[{\"titulo\":\"A\"},{\"titulo\":\"B\"}}}}");
        CHECK(sc2.ok() && sc2.args["tareas"].size() == 2, "sanitizador: arreglo cerrado con llave");
        // Nombre y argumentos encadenados, dentro de ```json (salida real).
        const std::string encadenado =
            "```json\n{\"tool_call\": \"lanzar_subagentes\": {\"tareas\": [{\"titulo\": \"Listar escenas\", "
            "\"instrucciones\": \"Lista las escenas del proyecto Demo\", \"contexto\": \"project=Demo\", "
            "\"perfil\": \"lectura\"}, {\"titulo\": \"Listar scripts\", \"instrucciones\": \"Lista los scripts\", "
            "\"contexto\": \"project=Demo\", \"perfil\": \"lectura\"}}]}\n```";
        bool reconocido = false;
        for (const auto& b : llm::PromptToolParser::llamadasSinCerco(encadenado)) {
            const auto sc3 = llm::ToolCallSanitizer::block(b);
            reconocido = reconocido || (sc3.ok() && sc3.name == "lanzar_subagentes" && sc3.args["tareas"].size() == 2);
        }
        CHECK(reconocido, "sanitizador: \"tool_call\": \"nombre\": {args}");
    }

    // Tope de tandas por pedido: la tercera llamada se rechaza sin ejecutar.
    {
        auto regT = std::make_shared<GatewayGuion::Registro>();
        funshi::Silvy insistente(harness);
        insistente.configurarConFabrica(
            [regT]() {
                return std::make_shared<GatewayGuion>(
                    [](const LlamadaVista& v) {
                        if (esSubagente(v)) return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText("RESULTADO: completado")};
                        return std::vector<llm::LlmChunk>{llm::LlmChunk::makeToolCall(
                            "t", "lanzar_subagentes",
                            R"({"tareas":[{"titulo":"A","instrucciones":"Consulta la versión e informa el número","perfil":"lectura"}]})")};
                    },
                    regT);
            },
            true, 1);
        funshi::SesionAgente st;
        int inicios = 0;
        std::vector<std::string> errores;
        insistente.procesar("delega", st, [&](const llm::AgentEvent& e) {
            if (e.type == llm::AgentEvent::Type::SubagenteInicio) ++inicios;
            if (e.type == llm::AgentEvent::Type::ToolEnd && e.origen.empty() && !e.ok) errores.push_back(e.result);
        });
        CHECK(inicios == 2, "subagentes: máximo 2 tandas por pedido (" + std::to_string(inicios) + ")");
        CHECK(!errores.empty() && errores.front().find("Ya delegaste") != std::string::npos,
              "subagentes: la tercera tanda se rechaza con explicación");
    }

    // Validación de tareas mal escritas: el modelo recibe por qué y cómo corregir.
    using funshi::agente::LanzarSubagentesTool;
    std::vector<funshi::agente::TareaSubagente> tareas;
    std::string error;
    CHECK(!LanzarSubagentesTool::leerTareas({{"tareas", nlohmann::json::array()}}, tareas, error),
          "tareas: lista vacía rechazada");
    CHECK(!LanzarSubagentesTool::leerTareas(
              {{"tareas", {{{"titulo", "x"}, {"instrucciones", "hazlo"}, {"perfil", "lectura"}}}}}, tareas, error) &&
              error.find("vagas") != std::string::npos,
          "tareas: instrucciones vagas rechazadas");
    tareas.clear();
    CHECK(LanzarSubagentesTool::leerTareas(
              {{"tareas", {{{"titulo", "x"}, {"instrucciones", "Consulta la versión e informa"}, {"perfil", "lectura/escena"}}}}},
              tareas, error) &&
              tareas.size() == 1 && tareas[0].perfil == "completo",
          "tareas: perfil mal escrito → completo");
    nlohmann::json cinco = nlohmann::json::array();
    for (int i = 0; i < 5; ++i) cinco.push_back({{"titulo", "t"}, {"instrucciones", "Consulta la versión e informa"}});
    CHECK(!LanzarSubagentesTool::leerTareas({{"tareas", cinco}}, tareas, error), "tareas: más de 4 rechazadas");
}

}  // namespace pruebas_agente
