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

#pragma once

#include "Agente/Harness.h"
#include "Nucleo/Solicitud.h"
#include "Agente/LLM/LlmTypes.h"
#include "Agente/LLM/LlmConfig.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace funshi::agente::llm {
    class LlmGateway;
    class ToolRegistry;
    class AgentLoop;
    struct LlmConfig;
}

namespace funshi {

// Orden procesada por Silvy: texto original, solicitud generada y si se logró
// interpretar.
struct OrdenProcesada {
    std::string textoOriginal;
    Solicitud solicitud;
    bool interpretada = false;
    std::string motivoRechazo;
};

// Silvy: agente que recibe texto. Si empieza con '/' ejecuta comando CLI directo.
// Si no, se lo pasa al agente LLM (si está configurado). Mantiene historial y
// auditoría para poder responder "qué cambiaste".
class Silvy {
public:
    explicit Silvy(Harness& harness);

    // Configura el agente LLM (requiere libcurl + config válida).
    // Devuelve true si se configuró correctamente.
    bool configurarLlm(const agente::llm::LlmConfig& config, bool nativeToolCalls = true);
    bool tieneLlm() const { return agentLoop_ != nullptr; }

    // Procesa una línea de texto:
    // - Si empieza con '/', ejecuta como comando CLI (project.create, etc.)
    // - Si no, la envía al agente LLM (requiere tenerLlm()).
    // Devuelve la respuesta textual (para mostrar en chat) y el resultado del comando si aplica.
    struct RespuestaProcesada {
        std::string texto;           // Texto para mostrar al usuario
        ResultadoComando resultado;  // Solo válido si fue comando CLI
        bool fueComandoCli = false;
        bool error = false;
    };
    RespuestaProcesada procesar(const std::string& texto,
                                std::function<void(const agente::llm::AgentEvent&)> onEvent = nullptr);

    // Limpia el historial de conversación del agente LLM.
    void limpiarHistorialLlm();

    // Devuelve el resumen de auditoría para mostrar al usuario qué se hizo.
    std::string resumenQueHice(std::size_t n = 10) const;

    // Devuelve el nombre del modelo LLM activo (si hay uno configurado), vacío si no.
    std::string modeloActivo() const;

    const Harness& harness() const { return harness_; }

private:
    Harness& harness_;
    std::vector<OrdenProcesada> historial_;

    // LLM Agent
    std::shared_ptr<agente::llm::LlmGateway> gateway_;
    std::shared_ptr<agente::llm::ToolRegistry> registry_;
    std::shared_ptr<agente::llm::AgentLoop> agentLoop_;
    std::vector<agente::llm::ChatMessage> conversationHistory_;
    std::string modeloActivoNombre_;  // Nombre del modelo activo (para UI)

    bool esJsonValido(const std::string& texto) const;
    OrdenProcesada interpretarTextoPlano(const std::string& texto);
    OrdenProcesada interpretar(const std::string& texto);
    RespuestaProcesada ejecutarComandoCli(const std::string& texto);
    RespuestaProcesada enviarAlLlm(const std::string& texto,
                                   std::function<void(const agente::llm::AgentEvent&)> onEvent);
};

}  // namespace funshi
