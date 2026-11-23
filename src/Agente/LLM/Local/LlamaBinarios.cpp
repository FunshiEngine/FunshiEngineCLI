/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Local/LlamaBinarios.h"

#include "Agente/LLM/Local/LlamaBinariosInterno.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <mutex>

namespace funshi::agente::llm {

namespace {

namespace fs = std::filesystem;
using detalle_llama::archivoRegistro;

// Estado compartido entre hilos: serializa instalaciones y guarda la última
// consulta para no repetirla en cada llamada.
// Los estáticos se crean con new y nunca se destruyen: un hilo en segundo
// plano puede seguir activo mientras el proceso termina.
std::mutex& mutexInstalacion() {
    static auto* m = new std::mutex;
    return *m;
}

struct ConsultaCache {
    detalle_llama::ReleaseLlama release;
    std::chrono::steady_clock::time_point cuando{};
    bool valida = false;
};
ConsultaCache& consultaCache() {
    static auto* c = new ConsultaCache;
    return *c;
}

std::mutex& mutexResumen() {
    static auto* m = new std::mutex;
    return *m;
}
ResumenLlama& resumenGlobal() {
    static auto* r = new ResumenLlama;
    return *r;
}
void publicarResumen(const ResumenLlama& r) {
    std::lock_guard<std::mutex> lock(mutexResumen());
    resumenGlobal() = r;
}

}  // namespace

PlataformaLlama detectarPlataformaLlama() {
    PlataformaLlama p;
#if defined(__x86_64__) || defined(_M_X64)
    p.arquitectura = "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    p.arquitectura = "arm64";
#else
    p.arquitectura = "desconocida";
#endif
#if defined(_WIN32)
    p.so = "windows";
    p.sufijoAsset = "bin-win-cpu-" + p.arquitectura + ".zip";
#elif defined(__APPLE__)
    p.so = "macos";
    p.sufijoAsset = "bin-macos-" + p.arquitectura + ".tar.gz";
#elif defined(__linux__)
    p.so = "linux";
    p.sufijoAsset = "bin-ubuntu-" + p.arquitectura + ".tar.gz";
#else
    p.so = "desconocido";
#endif
    p.soportada = p.arquitectura != "desconocida" && !p.sufijoAsset.empty();
    return p;
}

InstaladorLlama::InstaladorLlama(fs::path baseDir) : baseDir_(std::move(baseDir)) {}

InstalacionLlama InstaladorLlama::instalada() const {
    InstalacionLlama inst;
    try {
        std::ifstream f(archivoRegistro(directorioBinarios()));
        if (!f) return inst;
        nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (!j.is_object()) return inst;
        const fs::path servidor = j.value("server", std::string{});
        std::error_code ec;
        if (servidor.empty() || !fs::is_regular_file(servidor, ec)) return inst;
        inst.tag = j.value("tag", std::string{});
        inst.asset = j.value("asset", std::string{});
        inst.servidor = servidor;
    } catch (...) {
        return {};
    }
    return inst;
}

// Flujo: plataforma → última release (cacheada 10 min) → ¿ya instalada? →
// descargar e instalar. Ante cualquier fallo se usa la versión instalada.
EstadoLlamaServer InstaladorLlama::asegurarActualizado(const ProgresoLlama& progreso) {
    EstadoLlamaServer estado;
    auto informar = [&](double p, const std::string& s) {
        if (progreso) progreso(p, s);
        std::lock_guard<std::mutex> lock(mutexResumen());
        if (resumenGlobal().enCurso) {
            resumenGlobal().mensaje =
                p >= 0.0 ? s + " (" + std::to_string(static_cast<int>(p * 100)) + "%)" : s;
        }
    };
    publicarResumen({true, false, false, "Comprobando llama-server..."});

    try {
        std::lock_guard<std::mutex> lock(mutexInstalacion());
        const InstalacionLlama actual = instalada();
        const PlataformaLlama plataforma = detectarPlataformaLlama();

        // Termina usando lo ya instalado (si existe) y explica el motivo.
        auto usarInstalada = [&](const std::string& motivo, bool sinRed) {
            estado.sinConexion = sinRed;
            if (actual.valida()) {
                estado.ok = true;
                estado.tag = actual.tag;
                estado.servidor = actual.servidor;
                estado.mensaje = "llama-server " + actual.tag + " (" + motivo + ")";
            } else {
                estado.ok = false;
                estado.mensaje = "llama-server no está instalado: " + motivo;
            }
            publicarResumen({false, estado.ok, estado.sinConexion, estado.mensaje});
            return estado;
        };

        if (!plataforma.soportada) {
            return usarInstalada("plataforma sin binarios oficiales (" + plataforma.so + "/" +
                                     plataforma.arquitectura + ")",
                                 false);
        }

        // 1. Última versión publicada (cacheada 10 minutos).
        informar(-1.0, "Consultando la última versión de llama.cpp...");
        ConsultaCache& cache = consultaCache();
        const auto ahora = std::chrono::steady_clock::now();
        if (!cache.valida || ahora - cache.cuando > std::chrono::minutes(10)) {
            detalle_llama::ReleaseLlama nueva;
            const auto r = detalle_llama::buscarUltimaRelease(plataforma, nueva);
            if (!r.ok) return usarInstalada(r.error, r.sinConexion);
            cache = {nueva, ahora, true};
        }
        const auto& release = cache.release;

        // 2. Ya instalada la misma versión: nada que hacer.
        if (actual.valida() && actual.tag == release.tag) {
            estado.ok = true;
            estado.tag = actual.tag;
            estado.servidor = actual.servidor;
            estado.mensaje = "llama-server " + actual.tag + " (al día)";
            informar(1.0, estado.mensaje);
            publicarResumen({false, true, false, estado.mensaje});
            return estado;
        }

        // 3. Descargar, extraer e instalar la nueva versión.
        const std::string accion = actual.valida()
                                       ? "Actualizando llama-server " + actual.tag + " → " + release.tag
                                       : "Descargando llama-server " + release.tag;
        publicarResumen({true, false, false, accion + "..."});
        informar(0.0, accion + "...");
        fs::path servidor;
        const auto ri = detalle_llama::instalarRelease(directorioBinarios(), release, plataforma,
                                                       accion, informar, servidor);
        if (!ri.ok) return usarInstalada(ri.error, ri.sinConexion);

        estado.ok = true;
        estado.actualizado = true;
        estado.tag = release.tag;
        estado.servidor = servidor;
        estado.mensaje = "llama-server " + release.tag + " instalado";
        informar(1.0, estado.mensaje);
        publicarResumen({false, true, false, estado.mensaje});
        return estado;
    } catch (const std::exception& e) {
        estado.ok = false;
        estado.mensaje = std::string("Error preparando llama-server: ") + e.what();
    } catch (...) {
        estado.ok = false;
        estado.mensaje = "Error desconocido preparando llama-server";
    }
    // Último recurso tras una excepción: usar lo instalado si existe.
    const InstalacionLlama actual = instalada();
    if (actual.valida()) {
        estado.ok = true;
        estado.tag = actual.tag;
        estado.servidor = actual.servidor;
    }
    publicarResumen({false, estado.ok, false, estado.mensaje});
    return estado;
}

fs::path rutaLlamaServerInstalado(const fs::path& baseDir) {
    return InstaladorLlama(baseDir).instalada().servidor;
}

ResumenLlama resumenLlama() {
    std::lock_guard<std::mutex> lock(mutexResumen());
    return resumenGlobal();
}

}  // namespace funshi::agente::llm
