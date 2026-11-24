/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Cliente/AsignadorTuneles.h"

#include "Agente/LLM/Cliente/HttpDescarga.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace funshi::agente::llm {

namespace fs = std::filesystem;

AsignadorTuneles::AsignadorTuneles(Contar contar, Vaciar vaciar, Guardar guardar, Restaurar restaurar)
    : contar_(std::move(contar)), vaciar_(std::move(vaciar)), guardar_(std::move(guardar)),
      restaurar_(std::move(restaurar)) {}

std::string AsignadorTuneles::archivoDeSesion(const std::string& sesion) {
    // Solo letras, dígitos, '-' y '_': el servidor rechaza rutas en el nombre.
    std::string nombre;
    for (unsigned char c : sesion) nombre += (std::isalnum(c) || c == '-' || c == '_') ? static_cast<char>(c) : '_';
    return (nombre.empty() ? std::string("cli") : nombre) + ".bin";
}

std::shared_ptr<AsignadorTuneles> AsignadorTuneles::paraServidor(const std::string& baseUrl, const fs::path& carpetaSlots) {
    // La API va bajo /v1; /props y /slots están en la raíz del servidor.
    std::string raiz = baseUrl;
    while (!raiz.empty() && raiz.back() == '/') raiz.pop_back();
    if (raiz.size() >= 3 && raiz.compare(raiz.size() - 3, 3, "/v1") == 0) raiz.resize(raiz.size() - 3);
    const auto accion = [raiz](int slot, const std::string& que, const std::string& cuerpo) {
        std::string respuesta;
        return http::post(raiz + "/slots/" + std::to_string(slot) + "?action=" + que, cuerpo, respuesta, 60000).ok;
    };
    Guardar guardar;
    Restaurar restaurar;
    if (!carpetaSlots.empty()) {
        guardar = [accion, carpetaSlots](int slot, const std::string& sesion) {
            const bool ok = accion(slot, "save", nlohmann::json{{"filename", archivoDeSesion(sesion)}}.dump());
            podarCachesGuardadas(carpetaSlots, kMaxCachesGuardadas);
            return ok;
        };
        // El archivo se lee una vez: restaurado (o inservible) se borra.
        restaurar = [accion, carpetaSlots](int slot, const std::string& sesion) {
            const fs::path archivo = carpetaSlots / archivoDeSesion(sesion);
            std::error_code ec;
            if (!fs::is_regular_file(archivo, ec)) return false;
            const bool ok = accion(slot, "restore", nlohmann::json{{"filename", archivoDeSesion(sesion)}}.dump());
            fs::remove(archivo, ec);
            return ok;
        };
    }
    return std::make_shared<AsignadorTuneles>(
        [raiz]() {
            std::string cuerpo;
            if (!http::get(raiz + "/props", cuerpo, {}, "FunshiEngineCLI/1.0", 5000).ok) return 0;
            const auto j = nlohmann::json::parse(cuerpo, nullptr, false);
            return j.is_object() ? j.value("total_slots", 0) : 0;
        },
        [accion](int slot) { return accion(slot, "erase", "{}"); }, std::move(guardar), std::move(restaurar));
}

void AsignadorTuneles::podarCachesGuardadas(const fs::path& carpeta, std::size_t maximo) {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> archivos;
    for (const auto& e : fs::directory_iterator(carpeta, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".bin") archivos.emplace_back(e.last_write_time(ec), e.path());
    }
    if (archivos.size() <= maximo) return;
    std::sort(archivos.begin(), archivos.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = maximo; i < archivos.size(); ++i) fs::remove(archivos[i].second, ec);
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
    // Ya tiene uno: el mismo, con su caché.
    for (std::size_t i = 0; i < tuneles_.size(); ++i) {
        if (tuneles_[i].sesion == sesion) {
            ++tuneles_[i].reservas;
            return Reserva(shared_from_this(), static_cast<int>(i));
        }
    }
    // Uno libre, o el de la sesión que entró primero entre los no reservados.
    int elegido = -1;
    for (std::size_t i = 0; i < tuneles_.size(); ++i) {
        const auto& t = tuneles_[i];
        if (t.reservas > 0) continue;
        if (t.sesion.empty()) {
            elegido = static_cast<int>(i);
            break;
        }
        if (elegido < 0 || t.entrada < tuneles_[static_cast<std::size_t>(elegido)].entrada) elegido = static_cast<int>(i);
    }
    if (elegido < 0) return {};
    auto& t = tuneles_[static_cast<std::size_t>(elegido)];
    // La que sale guarda su caché; la que entra recupera la suya o empieza limpia.
    if (!t.sesion.empty() && guardar_) guardar_(elegido, t.sesion);
    const bool restaurada = restaurar_ && restaurar_(elegido, sesion);
    if (!restaurada && !t.sesion.empty() && vaciar_) vaciar_(elegido);
    t.sesion = sesion;
    t.reservas = 1;
    t.entrada = ++reloj_;
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
