/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — mensajes mientras Silvy procesa y edición de los propios:
//   - con la sesión ocupada, un mensaje queda en espera hasta elegir
//     agregarlo al razonamiento en curso (A) o encolarlo (E);
//   - lo que el agente quiere borrar espera la confirmación del usuario
//     (S sí · N no); cancelar el pedido cuenta como no;
//   - al terminar un pedido sigue la cola (y las aclaraciones que llegaron
//     cuando el bucle ya había cerrado);
//   - clic en un mensaje propio: editarlo (E) o eliminarlo (D). Ambas
//     cancelan lo que esté pensando y descartan ese mensaje y todo lo que
//     vino después, en el chat y en lo que el modelo recuerda.

#include "Tui/AplicacionTui.h"

#include <algorithm>
#include <set>

namespace funshi {

using chat::MensajeChat;

bool AplicacionTui::pedirConfirmacion(const std::shared_ptr<SesionTui>& sesion, const std::string& descripcion) {
    std::unique_lock<std::mutex> lock(chatMutex_);
    sesion->confirmacion = descripcion;
    sesion->respuestaConfirmacion = -1;
    sesion->enEspera.clear();
    sesion->menuMensaje = -1;
    lock.unlock();
    mensaje_ = "Silvy (" + sesion->datos.titulo + ") pide confirmación: S sí · N no.";
    mensajeEsError_ = false;
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    lock.lock();
    // Cancelar el pedido (Ctrl+X, editar o eliminar un mensaje) cuenta como no.
    const auto cancelar = sesion->datos.agente.cancelar;
    while (sesion->respuestaConfirmacion < 0 && !cancelar->load()) {
        confirmacionCv_.wait_for(lock, std::chrono::milliseconds(200));
    }
    const bool si = sesion->respuestaConfirmacion == 1;
    sesion->confirmacion.clear();
    sesion->respuestaConfirmacion = -1;
    return si;
}

bool AplicacionTui::eventoEnCurso(const ftxui::Event& evento) {
    std::shared_ptr<SesionTui> sesion;
    std::string espera;
    std::string confirmacion;
    int menu = -1;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        sesion = sesionActualSinLock();
        if (!sesion) return false;
        espera = sesion->enEspera;
        confirmacion = sesion->confirmacion;
        menu = sesion->menuMensaje;
    }
    if (espera.empty() && menu < 0 && confirmacion.empty()) return false;
    const auto tecla = [&](char c) {
        return evento == ftxui::Event::Character(c) ||
               evento == ftxui::Event::Character(static_cast<char>(c - 'a' + 'A'));
    };

    if (!confirmacion.empty()) {
        // Hasta responder, las demás teclas no hacen nada.
        const bool si = tecla('s') || tecla('y');
        if (!si && !tecla('n') && evento != ftxui::Event::Escape) return true;
        {
            std::lock_guard<std::mutex> lock(chatMutex_);
            sesion->respuestaConfirmacion = si ? 1 : 0;
        }
        confirmacionCv_.notify_all();
        mensaje_ = si ? "Autorizado: " + confirmacion : "No autorizado: " + confirmacion;
        mensajeEsError_ = false;
        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
        return true;
    }

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

    if (tecla('e') || tecla('d')) {
        descartarDesde(static_cast<std::size_t>(menu), tecla('e'));
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        sesion->menuMensaje = -1;
    }
    if (evento == ftxui::Event::Escape) {
        mensaje_.clear();
        if (pantalla_) pantalla_->PostEvent(ftxui::Event());
        return true;
    }
    return false;  // otra tecla: se cierra el menú y la tecla sigue su curso
}

void AplicacionTui::descartarDesde(std::size_t indice, bool editar) {
    std::shared_ptr<SesionTui> sesion;
    bool ocupada = false;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        sesion = sesionActualSinLock();
        if (!sesion) return;
        sesion->menuMensaje = -1;
        auto& ms = sesion->mensajes;
        if (indice >= ms.size() || ms[indice].rol != MensajeChat::Rol::Usuario || !ms[indice].ref.empty()) return;
        const std::string texto = ms[indice].texto;
        // Pedidos al modelo que se descartan (las órdenes con '/' no entran
        // en su historial) y subagentes lanzados por ellos.
        std::size_t pedidos = 0;
        std::set<std::string> subagentes;
        for (std::size_t i = indice; i < ms.size(); ++i) {
            if (ms[i].rol == MensajeChat::Rol::Usuario && ms[i].ref.empty() && ms[i].texto.rfind('/', 0) != 0) ++pedidos;
            if (ms[i].rol == MensajeChat::Rol::Subagente && !ms[i].ref.empty()) subagentes.insert(ms[i].ref);
        }
        ms.erase(ms.begin() + static_cast<std::ptrdiff_t>(indice), ms.end());
        auto& subs = sesion->subagentes;
        subs.erase(std::remove_if(subs.begin(), subs.end(),
                                  [&](const auto& s) { return subagentes.count(s->datos.id) > 0; }),
                   subs.end());
        if (subagentes.count(sesion->vista)) sesion->vista.clear();
        sesion->cola.clear();
        sesion->enEspera.clear();
        ++sesion->generacion;  // lo que siga llegando del pedido en curso ya no se muestra
        sesion->desplazamiento = 0;
        sesion->lineasDibujadas = -1;
        ocupada = sesion->ocupado;
        if (ocupada) {
            // El historial lo está usando el pedido en curso: se recorta cuando termine.
            sesion->pedidosADescartar += pedidos;
            sesion->datos.agente.cancelar->store(true);
            sesion->datos.agente.aclaraciones->tomar();
        } else {
            sesion->datos.agente.descartarPedidos(pedidos);
        }
        if (editar) textoChat_ = texto;
    }
    if (!ocupada) guardarSesion(sesion);
    if (editar) enfocarChat();
    mensaje_ = std::string(editar ? "Editando el mensaje" : "Mensaje eliminado") +
               ": se descartó lo que vino después. Lo que Silvy ya hizo en el proyecto no se deshace.";
    mensajeEsError_ = false;
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
}

void AplicacionTui::siguienteDeLaCola(std::shared_ptr<SesionTui> sesion) {
    std::string siguiente;
    bool yaEnElChat = false;  // aclaraciones tardías: su línea ya se ve
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        auto& agente = sesion->datos.agente;
        if (sesion->pedidosADescartar > 0) {
            agente.descartarPedidos(sesion->pedidosADescartar);
            sesion->pedidosADescartar = 0;
            agente.aclaraciones->tomar();
        }
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
