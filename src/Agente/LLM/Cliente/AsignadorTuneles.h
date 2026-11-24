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
// Las sesiones forman una cola sobre los slots: un slot se reserva mientras
// dura un pedido y, si hace falta uno para otra sesión y no hay libres, sale
// la sesión que entró primero entre las que no están reservadas (FIFO). Su
// caché se guarda en disco (POST /slots/<i>?action=save) para no recalcularla
// al volver; cuando la sesión vuelve a tener slot, se restaura su archivo
// (action=restore) y se borra. Si no hay archivo que restaurar, el slot se
// vacía (action=erase) antes de usarlo. Con todos los slots reservados el
// pedido va sin túnel (-1: elige el servidor). El historial de la
// conversación no depende de esto: está en la sesión; la caché solo ahorra
// volver a procesar el prompt.

#include <cstdint>
#include <filesystem>
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
    // Guarda la caché del slot como la de `sesion` / la restaura (false si no
    // había nada guardado o no se pudo).
    using Guardar = std::function<bool(int slot, const std::string& sesion)>;
    using Restaurar = std::function<bool(int slot, const std::string& sesion)>;
    AsignadorTuneles(Contar contar, Vaciar vaciar, Guardar guardar = nullptr, Restaurar restaurar = nullptr);

    // Para el servidor de `baseUrl` (la de la API, p. ej. http://127.0.0.1:11434/v1):
    // cuenta con GET /props (total_slots) y vacía con POST /slots/<i>?action=erase.
    // Con `carpetaSlots` (la --slot-save-path del servidor) guarda y restaura
    // la caché de cada sesión en <carpetaSlots>/<sesión>.bin.
    static std::shared_ptr<AsignadorTuneles> paraServidor(const std::string& baseUrl,
                                                          const std::filesystem::path& carpetaSlots = {});
    // Nombre de archivo de la caché guardada de una sesión.
    static std::string archivoDeSesion(const std::string& sesion);
    // Deja en `carpeta` solo las `maximo` cachés guardadas más recientes (cada
    // una ocupa cientos de MB; perder una solo obliga a recalcular el prompt).
    static void podarCachesGuardadas(const std::filesystem::path& carpeta, std::size_t maximo);
    static constexpr std::size_t kMaxCachesGuardadas = 4;

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
        std::string sesion;       // dueño de la caché del slot
        int reservas = 0;
        std::uint64_t entrada = 0;  // orden en que el dueño entró al slot (FIFO)
    };
    void liberar(int slot);

    Contar contar_;
    Vaciar vaciar_;
    Guardar guardar_;
    Restaurar restaurar_;
    std::mutex mutex_;
    std::vector<Tunel> tuneles_;
    bool contado_ = false;
    std::uint64_t reloj_ = 0;
};

}  // namespace funshi::agente::llm
