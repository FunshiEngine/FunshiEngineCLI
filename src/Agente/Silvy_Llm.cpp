/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Silvy — agente LLM: configuración del modelo y armado de cada pedido.
//
// Cada pedido crea su propio AgentLoop con su propio gateway: así varias
// sesiones (y sus subagentes) pueden correr a la vez sin compartir el handle
// de cURL. El bucle principal recibe:
//   - herramientas del perfil "completo" + buscar_comandos + ejecutar_comando;
//   - lanzar_subagentes, cuyo gestor crea bucles de perfiles más chicos;
//   - la documentación recuperada para el pedido (RAG) como contexto del turno.
#include "Agente/Silvy.h"

#include "Agente/Conocimiento/BaseConocimiento.h"
#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/LLM/Cliente/LlmGateway.h"
#include "Agente/LLM/Herramientas/PerfilesHerramientas.h"
#include "Agente/Subagentes/HerramientaSubagentes.h"
#include "Agente/Subagentes/PlantillasAgente.h"
#include "Nucleo/CatalogoComandos.h"

#include <set>

namespace funshi {

namespace llm = agente::llm;

namespace {

std::set<std::string> nombresHerramientas(const llm::ToolRegistry& registro) {
    std::set<std::string> nombres;
    for (const auto& spec : registro.specs()) nombres.insert(spec.name);
    return nombres;
}

// El modelo pidió un comando del catálogo como si fuera una herramienta
// (p. ej. scene.object.addcomponent): se ejecuta vía ejecutar_comando, que
// aplica el perfil del bucle.
bool aliasComando(const std::string& nombre, const nlohmann::json& args, std::string& real,
                  nlohmann::json& argsReales) {
    if (comandoDesdeTexto(nombre) == ComandoCLI::Desconocido) return false;
    real = "ejecutar_comando";
    argsReales = {{"comando", nombre}, {"argumentos", args.is_object() ? args : nlohmann::json::object()}};
    return true;
}

}  // namespace

bool Silvy::configurarLlm(const llm::LlmConfig& config, bool nativeToolCalls) {
    if (!config.validate().empty()) return false;
    if (config.mode != llm::LlmMode::REMOTE) return false;  // LOCAL usa llama-server vía REMOTE
    auto estado = std::make_shared<EstadoLlm>();
    estado->fabrica = [config]() -> std::shared_ptr<llm::LlmGateway> {
        return std::make_shared<llm::OpenAiCompatibleGateway>(
            config.remoteBaseUrl, config.apiKey, config.modelName, config.temperature,
            config.effectiveMaxTokens(), config.chatTimeoutMs, config.thinking, config.power);
    };
    estado->nativas = nativeToolCalls;
    estado->subagentesParalelos = config.subagentesParalelosEfectivo();
    estado->toolTimeoutMs = config.toolTimeoutMs;
    estado->modelo = config.modelName;
    try {
        estado->fabrica();  // valida que el gateway se pueda crear
    } catch (const std::exception&) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutexLlm_);
    llm_ = std::move(estado);
    sesionPorDefecto_.historial.clear();
    return true;
}

void Silvy::configurarConFabrica(FabricaGateway fabrica, bool nativeToolCalls, int subagentesParalelos,
                                 const std::string& nombreModelo) {
    auto estado = std::make_shared<EstadoLlm>();
    estado->fabrica = std::move(fabrica);
    estado->nativas = nativeToolCalls;
    estado->subagentesParalelos = subagentesParalelos;
    estado->modelo = nombreModelo;
    std::lock_guard<std::mutex> lock(mutexLlm_);
    llm_ = std::move(estado);
}

std::shared_ptr<const Silvy::EstadoLlm> Silvy::estadoLlm() const {
    std::lock_guard<std::mutex> lock(mutexLlm_);
    return llm_;
}

bool Silvy::tieneLlm() const { return estadoLlm() != nullptr; }

std::string Silvy::modeloActivo() const {
    const auto e = estadoLlm();
    return e ? e->modelo : std::string();
}

void Silvy::limpiarHistorialLlm() { sesionPorDefecto_.historial.clear(); }

Silvy::RespuestaProcesada Silvy::enviarAlLlm(const std::string& mensaje, SesionAgente& sesion,
                                             std::function<void(const llm::AgentEvent&)> onEvent) {
    RespuestaProcesada resp;
    const auto estado = estadoLlm();
    if (!estado) {
        resp.texto = "Agente LLM no configurado.";
        resp.error = true;
        return resp;
    }
    auto harness = std::make_shared<Harness>(harness_);

    // Bucles de subagentes: perfil acotado, gateway propio, sin delegar.
    agente::FabricaBucles fabricaSub = [estado, harness](const std::string& perfil) {
        agente::BucleCreado b;
        auto registro = llm::crearRegistroPerfil(harness, perfil);
        b.herramientas = nombresHerramientas(*registro);
        b.bucle = std::make_shared<llm::AgentLoop>(estado->fabrica(), registro, estado->nativas,
                                                   estado->toolTimeoutMs);
        b.bucle->setResolutorAlias(aliasComando);
        return b;
    };
    auto gestor = std::make_shared<agente::GestorSubagentes>(fabricaSub, estado->subagentesParalelos,
                                                             sesion.proyectoActivo);

    auto registro = llm::crearRegistroPerfil(harness, "completo");
    registro->add(std::make_shared<agente::LanzarSubagentesTool>(gestor));
    llm::AgentLoop bucle(estado->fabrica(), registro, estado->nativas, estado->toolTimeoutMs);
    bucle.setMaxIterations(10);
    bucle.setInstrucciones(agente::instruccionesPrincipal(true, sesion.proyectoActivo));
    sesion.cancelar->store(false);
    bucle.setCancelFlag(sesion.cancelar);
    bucle.setResolutorAlias(aliasComando);

    std::vector<std::string> usados;
    const std::string contexto = agente::conocimiento::BaseConocimiento::global()->contextoParaPedido(
        mensaje, nombresHerramientas(*registro), 4, &usados);
    if (onEvent) {
        std::string lista;
        for (const auto& id : usados) lista += (lista.empty() ? "" : ", ") + id;
        onEvent(llm::AgentEvent::estado(lista.empty() ? "documentación: ninguna relevante para el pedido"
                                                      : "documentación recuperada: " + lista));
    }
    sesion.historial.push_back(llm::ChatMessage::user(mensaje));
    const auto answer = bucle.run(sesion.historial, onEvent, contexto);

    resp.texto = answer.text.empty() ? "(sin respuesta)" : answer.text;
    return resp;
}

}  // namespace funshi
