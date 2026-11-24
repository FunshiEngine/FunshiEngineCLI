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
    // La documentación de lo que pide la aclaración (no siempre está en la del
    // pedido: "ponle un RigidBody" necesita la de addcomponent).
    const std::string doc = documentacionAclaracion_ ? documentacionAclaracion_(texto) : std::string();
    const auto agregarDoc = [&](std::size_t indice) {
        if (doc.empty() || indice >= messages.size()) return;
        recortesTurno_.emplace_back(indice, messages[indice].content.size());
        messages[indice].content += "\n\n" + doc;
    };
    const auto comoMensaje = [&] {
        messages.push_back(ChatMessage::user(std::string(kMarcaAclaracion) + " mientras trabajabas]\n" + texto +
                                             "\nTenla en cuenta en lo que sigue de este pedido."));
        agregarDoc(messages.size() - 1);
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
                agregarDoc(messages.size() - 1);
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
                // Con contexto del turno, Restaurar deja el pedido sin la
                // documentación; sin él, se recorta al terminar.
                if (pedidoOriginal.empty()) agregarDoc(indicePedido);
                else if (!doc.empty()) messages[indicePedido].content += "\n\n" + doc;
            } else {
                comoMensaje();
            }
            break;
        case FormatoAclaracion::EnSistema:
            if (!messages.empty() && messages.front().role == "system") {
                // Solo para este turno: al terminar se recorta (el sistema se rehace en cada turno).
                recortesTurno_.emplace_back(0, messages.front().content.size());
                messages.front().content += "\n\nACLARACIÓN DEL USUARIO (llegó mientras trabajabas): " + texto;
                if (!doc.empty()) messages.front().content += "\n" + doc;
            } else {
                comoMensaje();
            }
            break;
    }
    return true;
}

}  // namespace funshi::agente::llm
