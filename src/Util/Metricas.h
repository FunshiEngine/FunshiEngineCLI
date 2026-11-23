/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Métricas de rendimiento opcionales. Con FUNSHI_PERF=1 cada medición se
// agrega como una línea JSON a `<carpeta de config>/metricas.jsonl`.
#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>

namespace funshi::metricas {

// true si FUNSHI_PERF está activo (se evalúa una vez).
bool activas();

// Registra un evento {ts, tipo, ...datos}. No hace nada si no están activas.
void registrar(const std::string& tipo, nlohmann::json datos);

// Cronómetro de alcance: registra `tipo` con {ms, ...extra} al destruirse.
class Cronometro {
public:
    explicit Cronometro(std::string tipo, nlohmann::json extra = nlohmann::json::object());
    ~Cronometro();
    Cronometro(const Cronometro&) = delete;
    Cronometro& operator=(const Cronometro&) = delete;

private:
    std::string tipo_;
    nlohmann::json extra_;
    std::chrono::steady_clock::time_point inicio_;
    bool activo_;
};

}  // namespace funshi::metricas
