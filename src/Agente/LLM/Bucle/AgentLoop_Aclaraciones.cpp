/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AgentLoop — aclaraciones del usuario durante el turno: el buzón y su
// incorporación a la conversación según el formato elegido.
#include "Agente/LLM/Bucle/AgentLoop.h"

namespace funshi::agente::llm {

void BuzonInstrucciones::agregar(std::string texto) {
    std::lock_guard<std::mutex> lock(mutex_);
    pendientes_.push_back(std::move(texto));
}

std::vector<std::string> BuzonInstrucciones::tomar() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.swap(pendientes_);
    return out;
}

bool BuzonInstrucciones::vacio() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pendientes_.empty();
}

bool AgentLoop::incorporarAclaraciones(std::vector<ChatMessage>& messages, std::size_t indicePedido,
                                       std::string& pedidoOriginal, AgentAnswer& answer,
                                       const std::function<void(const AgentEvent&)>& emitir) {
    if (!buzon_) return false;
    const auto nuevas = buzon_->tomar();
    if (nuevas.empty()) return false;
    std::string texto;
    for (const auto& a : nuevas) {
        texto += (texto.empty() ? "" : "\n") + a;
        answer.aclaraciones.push_back(a);
        emitir(AgentEvent::estado("aclaración del usuario incorporada: " + a));
    }
    const auto comoMensaje = [&] {
        messages.push_back(ChatMessage::user("[Aclaración del usuario mientras trabajabas]\n" + texto +
                                             "\nTenla en cuenta en lo que sigue de este pedido."));
    };
    switch (formatoAclaracion_) {
        case FormatoAclaracion::MensajeNuevo:
            comoMensaje();
            break;
        case FormatoAclaracion::EnResultados:
            // En modo prompt los resultados vuelven como mensaje del usuario.
            if (!messages.empty() && messages.back().role == "user" &&
                messages.back().content.rfind(kInicioSalida, 0) == 0) {
                messages.back().content += "\n\nEl usuario agregó mientras trabajabas: " + texto;
            } else {
                comoMensaje();
            }
            break;
        case FormatoAclaracion::EnPedido:
            if (indicePedido < messages.size()) {
                const std::string extra = "\nAclaración del usuario: " + texto;
                messages[indicePedido].content += extra;
                // El historial guarda el pedido sin el contexto del turno, con la aclaración.
                if (!pedidoOriginal.empty()) pedidoOriginal += extra;
            } else {
                comoMensaje();
            }
            break;
        case FormatoAclaracion::EnSistema:
            if (!messages.empty() && messages.front().role == "system") {
                messages.front().content += "\n\nACLARACIÓN DEL USUARIO (llegó mientras trabajabas): " + texto;
            } else {
                comoMensaje();
            }
            break;
    }
    return true;
}

}  // namespace funshi::agente::llm
