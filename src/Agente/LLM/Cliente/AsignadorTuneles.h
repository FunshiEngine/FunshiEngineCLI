/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Túneles hacia un llama-server local: cada sesión de chat usa su propio
// slot (`id_slot`) y con él su propia caché KV. Sin esto, la caché de una
// sesión la reaprovechaba otra (--cache-reuse desplaza trozos calculados con
// la conversación anterior) y el modelo arrastraba cosas de otro chat.
//
// Un slot se reserva mientras dura un pedido. Si hace falta uno para otra
// sesión y no hay libres, se toma el menos usado recientemente entre los que
// no están reservados y se vacía antes (POST /slots/<i>?action=erase). Si
// todos están reservados, el pedido va sin túnel (-1: elige el servidor).

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace funshi::agente::llm {

class AsignadorTuneles : public std::enable_shared_from_this<AsignadorTuneles> {
public:
    using Contar = std::function<int()>;          // slots del servidor (<= 0: desconocido)
    using Vaciar = std::function<bool(int slot)>;
    AsignadorTuneles(Contar contar, Vaciar vaciar);

    // Para el servidor de `baseUrl` (la de la API, p. ej. http://127.0.0.1:11434/v1):
    // cuenta con GET /props (total_slots) y vacía con POST /slots/<i>?action=erase.
    static std::shared_ptr<AsignadorTuneles> paraServidor(const std::string& baseUrl);

    // Reserva del túnel de una sesión; lo libera al destruirse.
    class Reserva {
    public:
        Reserva() = default;
        Reserva(std::shared_ptr<AsignadorTuneles> dueno, int slot) : dueno_(std::move(dueno)), slot_(slot) {}
        Reserva(const Reserva&) = delete;
        Reserva& operator=(const Reserva&) = delete;
        Reserva(Reserva&& o) noexcept : dueno_(std::move(o.dueno_)), slot_(o.slot_) { o.slot_ = -1; }
        ~Reserva();
        int slot() const { return slot_; }

    private:
        std::shared_ptr<AsignadorTuneles> dueno_;
        int slot_ = -1;
    };
    Reserva reservar(const std::string& sesion);

    int total();  // slots del servidor (consultado una vez); 0 si no se sabe

private:
    struct Tunel {
        std::string sesion;     // dueño de la caché del slot
        int reservas = 0;
        std::uint64_t uso = 0;  // marca de último uso (LRU)
    };
    void liberar(int slot);

    Contar contar_;
    Vaciar vaciar_;
    std::mutex mutex_;
    std::vector<Tunel> tuneles_;
    bool contado_ = false;
    std::uint64_t reloj_ = 0;
};

}  // namespace funshi::agente::llm
