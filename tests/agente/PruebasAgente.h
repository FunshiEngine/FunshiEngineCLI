/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: pruebas del agente sin modelo real (RAG, herramientas,
// subagentes, sesiones). El modelo se reemplaza por GatewayGuion.
#pragma once

#include "Agente/LLM/Cliente/LlmGateway.h"

#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace pruebas_agente {

extern int total;
extern int fallos;

}  // namespace pruebas_agente

#define CHECK(cond, msg) do { ++::pruebas_agente::total; if (!(cond)) { ++::pruebas_agente::fallos; std::cout << "FALLO: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; } } while (0)

namespace pruebas_agente {

namespace llm = funshi::agente::llm;

// Lo que el gateway falso vio en una llamada.
struct LlamadaVista {
    std::vector<llm::ChatMessage> mensajes;
    std::vector<std::string> herramientas;
    int tunel = -1;  // slot pedido con setTunel
};

// Decide la respuesta a partir de los mensajes: devuelve los chunks a emitir.
using Guion = std::function<std::vector<llm::LlmChunk>(const LlamadaVista&)>;

// Modelo falso: registra cada llamada y responde según el guion. Varias
// instancias comparten el registro (subagentes en paralelo).
class GatewayGuion : public llm::LlmGateway {
public:
    struct Registro {
        std::mutex mutex;
        std::vector<LlamadaVista> llamadas;
    };
    GatewayGuion(Guion guion, std::shared_ptr<Registro> registro)
        : guion_(std::move(guion)), registro_(std::move(registro)) {}

    bool chatStream(const std::vector<llm::ChatMessage>& messages, const std::vector<llm::ToolSpec>* tools,
                    std::function<void(const llm::LlmChunk&)> onChunk) override {
        LlamadaVista vista;
        vista.mensajes = messages;
        vista.tunel = tunel_;
        if (tools) for (const auto& t : *tools) vista.herramientas.push_back(t.name);
        {
            std::lock_guard<std::mutex> lock(registro_->mutex);
            registro_->llamadas.push_back(vista);
        }
        for (const auto& c : guion_(vista)) onChunk(c);
        return true;
    }
    std::string chatText(const std::vector<llm::ChatMessage>&) override { return {}; }
    std::string chat(const std::vector<llm::ChatMessage>&) override { return {}; }
    std::vector<std::string> listModels() override { return {"guion"}; }
    void close() override {}
    void setTunel(int slot) override { tunel_ = slot; }

private:
    int tunel_ = -1;
    Guion guion_;
    std::shared_ptr<Registro> registro_;
};

void probarConocimiento();
void probarHerramientas();
void probarSubagentes();
void probarSesiones();
void probarMonitor();
void probarPreferencias();
void probarUsoDeComandos();
void probarDocumentos();
void probarTareas();
void probarTuneles();
void probarChat();

}  // namespace pruebas_agente
