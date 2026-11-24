/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — mensajes mientras Silvy procesa:
//   - con la sesión ocupada, un mensaje queda en espera hasta elegir
//     agregarlo al razonamiento en curso (A) o encolarlo (E);
//   - al terminar un pedido sigue la cola (y las aclaraciones que llegaron
//     cuando el bucle ya había cerrado).

#include "Tui/AplicacionTui.h"

#include <algorithm>

namespace funshi {

using chat::MensajeChat;

bool AplicacionTui::eventoEnCurso(const ftxui::Event& evento) {
    std::shared_ptr<SesionTui> sesion;
    std::string espera;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        sesion = sesionActualSinLock();
        if (!sesion) return false;
        espera = sesion->enEspera;
    }
    if (espera.empty()) return false;
    const auto tecla = [&](char c) {
        return evento == ftxui::Event::Character(c) ||
               evento == ftxui::Event::Character(static_cast<char>(c - 'a' + 'A'));
    };

    if (!espera.empty()) {
        if (tecla('a') || tecla('e')) {
            const bool agregar = tecla('a');
            bool sigueOcupada = false;
            {
                std::lock_guard<std::mutex> lock(chatMutex_);
                sesion->enEspera.clear();
                sigueOcupada = sesion->ocupado;
                if (sigueOcupada && agregar) {
                    sesion->datos.agente.aclaraciones->agregar(espera);
                    MensajeChat m;
                    m.rol = MensajeChat::Rol::Usuario;
                    m.texto = espera;
                    m.ref = "aclaracion";
                    sesion->mensajes.push_back(std::move(m));
                } else if (sigueOcupada) {
                    sesion->cola.push_back(espera);
                    MensajeChat m;
                    m.rol = MensajeChat::Rol::Estado;
                    m.texto = "en cola: «" + espera + "» (se envía cuando termine)";
                    sesion->mensajes.push_back(std::move(m));
                }
                sesion->desplazamiento = 0;
            }
            if (!sigueOcupada) lanzarOrdenChat(sesion, espera);  // terminó mientras elegía
            mensaje_ = !sigueOcupada ? "Ya había terminado: lo envié como pedido nuevo."
                       : agregar     ? "Agregado a lo que Silvy está haciendo."
                                     : "En cola: se envía cuando termine.";
        } else if (evento == ftxui::Event::Escape) {
            {
                std::lock_guard<std::mutex> lock(chatMutex_);
                sesion->enEspera.clear();
                textoChat_ = espera;
            }
            mensaje_ = "El mensaje volvió a la entrada.";
        } else {
            return true;  // hasta elegir, el mensaje no se pierde ni se mezcla con otra tecla
        }
        mensajeEsError_ = false;
        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
        return true;
    }

    return false;
}

void AplicacionTui::siguienteDeLaCola(std::shared_ptr<SesionTui> sesion) {
    std::string siguiente;
    bool yaEnElChat = false;  // aclaraciones tardías: su línea ya se ve
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        auto& agente = sesion->datos.agente;
        // Aclaraciones que llegaron cuando el bucle ya había terminado: van
        // como el pedido siguiente, antes que la cola.
        const auto tardias = agente.aclaraciones->tomar();
        if (!tardias.empty() && !sesion->ocupado) {
            for (const auto& t : tardias) siguiente += (siguiente.empty() ? "" : "\n") + t;
            // La primera de sus líneas pasa a ser el pedido (editable); las
            // demás quedan como parte de él.
            for (auto& m : sesion->mensajes) {
                if (m.ref == "aclaracion" && m.texto == tardias.front()) {
                    m.ref.clear();
                    m.tocar();
                    break;
                }
            }
            yaEnElChat = true;
        } else if (!sesion->cola.empty() && !sesion->ocupado) {
            siguiente = sesion->cola.front();
            sesion->cola.pop_front();
        }
    }
    guardarSesion(sesion);
    if (!siguiente.empty()) lanzarOrdenChat(sesion, siguiente, !yaEnElChat);
}

}  // namespace funshi
