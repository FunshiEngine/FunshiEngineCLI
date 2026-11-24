/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Util/Metricas.h"

#include "Agente/LLM/Config/LlmConfig.h"

#include <cstdlib>
#include <fstream>
#include <mutex>

namespace funshi::metricas {

namespace {

std::mutex& mutexArchivo() {
    static auto* m = new std::mutex;  // nunca se destruye: hay hilos en segundo plano
    return *m;
}

}  // namespace

bool activas() {
    static const bool valor = [] {
        const char* v = std::getenv("FUNSHI_PERF");
        return v && *v && std::string(v) != "0";
    }();
    return valor;
}

void registrar(const std::string& tipo, nlohmann::json datos) {
    if (!activas()) return;
    try {
        if (!datos.is_object()) datos = nlohmann::json{{"valor", datos}};
        datos["tipo"] = tipo;
        datos["ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        const auto ruta = agente::llm::LlmConfig::configPath().parent_path() / "metricas.jsonl";
        std::lock_guard<std::mutex> lock(mutexArchivo());
        std::error_code ec;
        std::filesystem::create_directories(ruta.parent_path(), ec);
        std::ofstream f(ruta, std::ios::app);
        f << datos.dump() << '\n';
    } catch (...) {
        // Las métricas nunca deben afectar el funcionamiento.
    }
}

Cronometro::Cronometro(std::string tipo, nlohmann::json extra)
    : tipo_(std::move(tipo)), extra_(std::move(extra)),
      inicio_(std::chrono::steady_clock::now()), activo_(activas()) {}

Cronometro::~Cronometro() {
    if (!activo_) return;
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - inicio_)
                        .count();
    extra_["ms"] = static_cast<double>(us) / 1000.0;
    registrar(tipo_, std::move(extra_));
}

}  // namespace funshi::metricas
