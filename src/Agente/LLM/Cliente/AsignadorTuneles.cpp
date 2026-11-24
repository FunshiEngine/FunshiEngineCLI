/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Cliente/AsignadorTuneles.h"

#include "Agente/LLM/Cliente/HttpDescarga.h"

#include <nlohmann/json.hpp>

namespace funshi::agente::llm {

AsignadorTuneles::AsignadorTuneles(Contar contar, Vaciar vaciar)
    : contar_(std::move(contar)), vaciar_(std::move(vaciar)) {}

std::shared_ptr<AsignadorTuneles> AsignadorTuneles::paraServidor(const std::string& baseUrl) {
    // La API va bajo /v1; /props y /slots están en la raíz del servidor.
    std::string raiz = baseUrl;
    while (!raiz.empty() && raiz.back() == '/') raiz.pop_back();
    if (raiz.size() >= 3 && raiz.compare(raiz.size() - 3, 3, "/v1") == 0) raiz.resize(raiz.size() - 3);
    return std::make_shared<AsignadorTuneles>(
        [raiz]() {
            std::string cuerpo;
            if (!http::get(raiz + "/props", cuerpo, {}, "FunshiEngineCLI/1.0", 5000).ok) return 0;
            const auto j = nlohmann::json::parse(cuerpo, nullptr, false);
            return j.is_object() ? j.value("total_slots", 0) : 0;
        },
        [raiz](int slot) {
            std::string respuesta;
            return http::post(raiz + "/slots/" + std::to_string(slot) + "?action=erase", "{}", respuesta, 10000).ok;
        });
}

int AsignadorTuneles::total() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!contado_) {
        const int n = contar_ ? contar_() : 0;
        // Si el servidor todavía no responde, se vuelve a consultar en la
        // próxima reserva.
        if (n > 0) {
            tuneles_.assign(static_cast<std::size_t>(n), Tunel{});
            contado_ = true;
        }
    }
    return static_cast<int>(tuneles_.size());
}

AsignadorTuneles::Reserva AsignadorTuneles::reservar(const std::string& sesion) {
    if (total() <= 0) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    ++reloj_;
    // Ya tiene uno: el mismo, con su caché.
    for (std::size_t i = 0; i < tuneles_.size(); ++i) {
        if (tuneles_[i].sesion == sesion) {
            ++tuneles_[i].reservas;
            tuneles_[i].uso = reloj_;
            return Reserva(shared_from_this(), static_cast<int>(i));
        }
    }
    // Uno libre, o el menos usado entre los no reservados (se vacía).
    int elegido = -1;
    for (std::size_t i = 0; i < tuneles_.size(); ++i) {
        const auto& t = tuneles_[i];
        if (t.reservas > 0) continue;
        if (t.sesion.empty()) {
            elegido = static_cast<int>(i);
            break;
        }
        if (elegido < 0 || t.uso < tuneles_[static_cast<std::size_t>(elegido)].uso) elegido = static_cast<int>(i);
    }
    if (elegido < 0) return {};
    auto& t = tuneles_[static_cast<std::size_t>(elegido)];
    if (!t.sesion.empty() && vaciar_) vaciar_(elegido);
    t.sesion = sesion;
    t.reservas = 1;
    t.uso = reloj_;
    return Reserva(shared_from_this(), elegido);
}

void AsignadorTuneles::liberar(int slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot < 0 || static_cast<std::size_t>(slot) >= tuneles_.size()) return;
    auto& t = tuneles_[static_cast<std::size_t>(slot)];
    if (t.reservas > 0) --t.reservas;
}

AsignadorTuneles::Reserva::~Reserva() {
    if (dueno_ && slot_ >= 0) dueno_->liberar(slot_);
}

}  // namespace funshi::agente::llm
