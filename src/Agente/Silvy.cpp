/*
 * Copyright 2024 FunshiEngine
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Agente/Silvy.h"
#include "Agente/LLM/AgentLoop.h"
#include "Agente/LLM/LlmGateway.h"
#include "Agente/LLM/EngineTools.h"
#include "Agente/LLM/EngineToolFactory.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace funshi {

namespace {

std::string minusculas(std::string texto) {
    std::transform(texto.begin(), texto.end(), texto.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return texto;
}

std::vector<std::string> partirPorEspacios(const std::string& texto) {
    std::vector<std::string> tokens;
    std::istringstream iss(texto);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

}  // namespace

Silvy::Silvy(Harness& harness) : harness_(harness) {}

bool Silvy::configurarLlm(const agente::llm::LlmConfig& config, bool nativeToolCalls) {
    auto errores = config.validate();
    if (!errores.empty()) {
        return false;
    }

    try {
        if (config.mode == agente::llm::LlmMode::REMOTE) {
            gateway_ = std::make_shared<agente::llm::OpenAiCompatibleGateway>(
                config.remoteBaseUrl,
                config.apiKey,
                config.modelName,
                config.temperature,
                config.effectiveMaxTokens(),
                config.chatTimeoutMs,
                config.thinking,
                config.power
            );
            modeloActivoNombre_ = config.modelName;
        } else {
            // LOCAL mode - por ahora no implementado (requiere llama-server)
            return false;
        }

        registry_ = agente::llm::createEngineToolRegistry(std::make_shared<Harness>(harness_));
        agentLoop_ = std::make_shared<agente::llm::AgentLoop>(gateway_, registry_, nativeToolCalls, config.toolTimeoutMs);
        agentLoop_->setMaxIterations(8);
        conversationHistory_.clear();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void Silvy::limpiarHistorialLlm() {
    conversationHistory_.clear();
}

Silvy::RespuestaProcesada Silvy::procesar(const std::string& texto,
                                          std::function<void(const agente::llm::AgentEvent&)> onEvent) {
    RespuestaProcesada resp;
    resp.texto = "";
    resp.fueComandoCli = false;
    resp.error = false;

    if (texto.empty()) {
        resp.texto = "";
        return resp;
    }

    // Prefijo '/' = comando CLI directo
    if (texto[0] == '/') {
        return ejecutarComandoCli(texto.substr(1));
    }

    // Sin prefijo = agente LLM
    if (!agentLoop_) {
        resp.texto = "[Error] Agente LLM no configurado. Usa `/modelo` en la TUI o configura LlmConfig.";
        resp.error = true;
        return resp;
    }

    return enviarAlLlm(texto, onEvent);
}

Silvy::RespuestaProcesada Silvy::ejecutarComandoCli(const std::string& texto) {
    RespuestaProcesada resp;
    resp.fueComandoCli = true;

    OrdenProcesada orden = interpretar(texto);
    historial_.push_back(orden);

    if (!orden.interpretada) {
        ResultadoComando resultado =
            ResultadoComando::fallo("orden_no_interpretada",
                                    orden.motivoRechazo,
                                    codigosSalida::kErrorUso);
        resp.resultado = resultado;
        resp.texto = "❌ " + orden.motivoRechazo;
        resp.error = true;

        EntradaAuditoria entrada;
        entrada.marcaTiempo = harness_.auditoria().ahoraIso8601();
        entrada.comando = "<no interpretado>";
        entrada.argumentos = nlohmann::json::object();
        entrada.ok = false;
        entrada.codigoError = "orden_no_interpretada";
        entrada.mensajeError = orden.motivoRechazo;
        entrada.codigoSalida = resultado.codigoSalida;
        entrada.modificaDisco = false;
        entrada.observaciones = "textoOriginal=" + texto;
        const_cast<Auditoria&>(harness_.auditoria()).registrar(entrada);
        return resp;
    }

    resp.resultado = harness_.ejecutar(orden.solicitud, /*modificaDisco=*/false);
    
    if (resp.resultado.respuesta.correcta) {
        resp.texto = "✅ Comando ejecutado: " + orden.solicitud.comando;
        if (!resp.resultado.respuesta.resultado.empty()) {
            resp.texto += "\n" + resp.resultado.respuesta.resultado.dump(2);
        }
    } else {
        resp.texto = "❌ " + resp.resultado.respuesta.codigoError + ": " + resp.resultado.respuesta.mensajeError;
        resp.error = true;
    }
    return resp;
}

Silvy::RespuestaProcesada Silvy::enviarAlLlm(const std::string& mensaje,
                                             std::function<void(const agente::llm::AgentEvent&)> onEvent) {
    RespuestaProcesada resp;
    resp.fueComandoCli = false;

    // Añadir mensaje del usuario al historial
    conversationHistory_.push_back(agente::llm::ChatMessage::user(mensaje));

    // Ejecutar el loop
    auto answer = agentLoop_->run(conversationHistory_, onEvent);

    // Añadir respuesta del asistente al historial
    if (!answer.text.empty()) {
        conversationHistory_.push_back(agente::llm::ChatMessage::assistant(answer.text));
    }

    resp.texto = answer.text.empty() ? "(sin respuesta)" : answer.text;
    resp.error = false;
    return resp;
}

bool Silvy::esJsonValido(const std::string& texto) const {
    try {
        nlohmann::json j = nlohmann::json::parse(texto);
        return j.is_object() && j.contains("command") &&
               j["command"].is_string();
    } catch (...) {
        return false;
    }
}

OrdenProcesada Silvy::interpretarTextoPlano(const std::string& texto) {
    OrdenProcesada orden;
    orden.textoOriginal = texto;

    std::string textoLimpio = texto;
    if (!textoLimpio.empty() && textoLimpio.front() == '"' &&
        textoLimpio.back() == '"') {
        textoLimpio = textoLimpio.substr(1, textoLimpio.size() - 2);
    }

    std::string bajo = minusculas(textoLimpio);
    std::vector<std::string> tokens = partirPorEspacios(bajo);

    if (tokens.empty()) {
        orden.motivoRechazo = "Orden vacía.";
        return orden;
    }

    if (bajo == "ayuda" || bajo == "help" || bajo == "?") {
        orden.motivoRechazo =
            "Escribe una orden como: 'crear proyecto MiJuego 2d', "
            "'listar proyectos', o pega una solicitud JSON.";
        return orden;
    }

    // --- project.create: "crear proyecto <nombre> [2d|3d]" ---
    if (bajo.find("crear proyecto") == 0 ||
        bajo.find("proyecto crear") == 0 ||
        bajo.find("create project") == 0) {
        std::string resto = bajo;
        std::string prefijos[] = {"crear proyecto ", "proyecto crear ",
                                  "create project "};
        for (const std::string& p : prefijos) {
            if (resto.find(p) == 0) {
                resto = resto.substr(p.size());
                break;
            }
        }
        std::vector<std::string> partes = partirPorEspacios(resto);
        if (partes.empty()) {
            orden.motivoRechazo =
                "Falta el nombre del proyecto. Ejemplo: crear proyecto MiJuego 2d";
            return orden;
        }
        orden.solicitud.comando = "project.create";
        orden.solicitud.argumentos["name"] = partes[0];
        int dim = 2;
        if (partes.size() >= 2) {
            if (partes[1] == "3d" || partes[1] == "3") dim = 3;
            else if (partes[1] == "2d" || partes[1] == "2") dim = 2;
        }
        orden.solicitud.argumentos["dimension"] = dim;
        orden.interpretada = true;
        return orden;
    }

    // --- project.list: "listar proyectos" ---
    if (bajo.find("listar proyecto") == 0 ||
        bajo.find("proyectos") == 0 ||
        bajo == "ls" || bajo == "list projects") {
        orden.solicitud.comando = "project.list";
        orden.solicitud.argumentos = nlohmann::json::object();
        orden.interpretada = true;
        return orden;
    }

    // --- diagnostics.list: "diagnósticos" / "ver diagnósticos" ---
    if (bajo.find("diagnóstico") != std::string::npos ||
        bajo.find("diagnostico") != std::string::npos) {
        orden.solicitud.comando = "diagnostics.list";
        orden.solicitud.argumentos = nlohmann::json::object();
        orden.interpretada = true;
        return orden;
    }

    // --- version: "versión" ---
    if (bajo == "versión" || bajo == "version") {
        orden.solicitud.comando = "version";
        orden.solicitud.argumentos = nlohmann::json::object();
        orden.interpretada = true;
        return orden;
    }

    // --- help: "comandos" ---
    if (bajo == "comandos" || bajo == "lista comandos") {
        orden.solicitud.comando = "help";
        orden.solicitud.argumentos = nlohmann::json::object();
        orden.interpretada = true;
        return orden;
    }

    orden.motivoRechazo =
        "No pude interpretar la orden. Usa ayuda para ver ejemplos o pega un JSON con 'command'.\n";
    return orden;
}

OrdenProcesada Silvy::interpretar(const std::string& texto) {
    OrdenProcesada orden;
    orden.textoOriginal = texto;

    if (texto.empty()) {
        orden.motivoRechazo = "Orden vacía.";
        return orden;
    }

    // Si es una solicitud JSON completa, usarla directamente.
    if (esJsonValido(texto)) {
        try {
            nlohmann::json j = nlohmann::json::parse(texto);
            orden.solicitud.comando = j["command"].get<std::string>();
            orden.solicitud.argumentos =
                j.value("args", nlohmann::json::object());
            if (j.contains("id")) {
                orden.solicitud.id = j["id"];
            }
            orden.interpretada = true;
            return orden;
        } catch (const nlohmann::json::exception& e) {
            orden.motivoRechazo = "JSON inválido: " + std::string(e.what());
            return orden;
        }
    }

    return interpretarTextoPlano(texto);
}

std::string Silvy::resumenQueHice(std::size_t n) const {
    return harness_.auditoria().resumenReciente(n);
}

std::string Silvy::modeloActivo() const {
    if (!agentLoop_ || !gateway_) return "";
    return modeloActivoNombre_;
}

}  // namespace funshi
