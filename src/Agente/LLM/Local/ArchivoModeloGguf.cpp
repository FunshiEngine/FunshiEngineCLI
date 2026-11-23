/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Local/ArchivoModeloGguf.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace funshi::agente::llm {
namespace {

// Lector mínimo de GGUF (v2/v3, little-endian). Lanza std::runtime_error
// ante datos truncados o fuera de rango.
class LectorGguf {
public:
    explicit LectorGguf(std::ifstream& in) : in_(in) {}

    template <typename T> T leer() {
        T v{};
        in_.read(reinterpret_cast<char*>(&v), sizeof(T));
        if (!in_) throw std::runtime_error("cabecera GGUF truncada");
        return v;
    }
    void saltar(std::uint64_t n) {
        // Lecturas a un búfer en vez de seekg: con miles de strings cortas
        // (vocabulario) seekg descarta el búfer de ifstream en cada salto.
        char buf[4096];
        while (n > 0) {
            const auto trozo = static_cast<std::streamsize>(std::min<std::uint64_t>(n, sizeof(buf)));
            in_.read(buf, trozo);
            if (!in_) throw std::runtime_error("cabecera GGUF truncada");
            n -= static_cast<std::uint64_t>(trozo);
        }
    }
    std::string leerTexto() {
        const auto n = leer<std::uint64_t>();
        if (n > (1u << 20)) throw std::runtime_error("texto GGUF fuera de rango");
        std::string s(static_cast<std::size_t>(n), '\0');
        if (n) {
            in_.read(s.data(), static_cast<std::streamsize>(n));
            if (!in_) throw std::runtime_error("cabecera GGUF truncada");
        }
        return s;
    }
    void saltarTexto() {
        const auto n = leer<std::uint64_t>();
        if (n > (1u << 20)) throw std::runtime_error("texto GGUF fuera de rango");
        saltar(n);
    }
    // Salta un valor de metadatos; si es uint32/uint64 lo devuelve en `num`.
    void saltarValor(std::uint32_t tipo, std::uint64_t* num = nullptr, int profundidad = 0) {
        static constexpr int kTam[] = {1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8};
        if (tipo == 8) { saltarTexto(); return; }
        if (tipo == 9) {
            if (profundidad > 2) throw std::runtime_error("arreglo GGUF anidado");
            const auto sub = leer<std::uint32_t>();
            const auto n = leer<std::uint64_t>();
            if (sub == 8 || sub == 9) {
                for (std::uint64_t i = 0; i < n; ++i) saltarValor(sub, nullptr, profundidad + 1);
            } else if (sub < 13) {
                saltar(n * static_cast<std::uint64_t>(kTam[sub]));
            } else {
                throw std::runtime_error("tipo GGUF desconocido");
            }
            return;
        }
        if (tipo == 4) { const auto v = leer<std::uint32_t>(); if (num) *num = v; return; }
        if (tipo == 10) { const auto v = leer<std::uint64_t>(); if (num) *num = v; return; }
        if (tipo >= 13) throw std::runtime_error("tipo GGUF desconocido");
        saltar(static_cast<std::uint64_t>(kTam[tipo]));
    }
    std::uint64_t posicion() { return static_cast<std::uint64_t>(in_.tellg()); }

private:
    std::ifstream& in_;
};

EstadoArchivoModelo inspeccionarSinCache(const std::filesystem::path& ruta, std::uint64_t tamano) {
    EstadoArchivoModelo estado;
    std::ifstream in(ruta, std::ios::binary);
    if (!in) {
        estado.completo = false;
        estado.detalle = "no se puede leer";
        return estado;
    }
    char magia[4] = {};
    in.read(magia, 4);
    if (!in || std::string(magia, 4) != "GGUF") {
        return estado;  // no es GGUF: no se puede verificar, se acepta
    }
    estado.esGguf = true;
    try {
        LectorGguf lector(in);
        const auto version = lector.leer<std::uint32_t>();
        if (version < 2 || version > 3) {
            estado.detalle = "versión GGUF " + std::to_string(version) + " no verificable";
            return estado;
        }
        const auto nTensores = lector.leer<std::uint64_t>();
        const auto nKv = lector.leer<std::uint64_t>();
        if (nTensores > 1000000 || nKv > 1000000) throw std::runtime_error("cabecera GGUF inválida");

        std::uint64_t alineacion = 32;
        for (std::uint64_t i = 0; i < nKv; ++i) {
            const std::string clave = lector.leerTexto();
            const auto tipo = lector.leer<std::uint32_t>();
            std::uint64_t num = 0;
            lector.saltarValor(tipo, &num);
            if (clave == "general.alignment" && num > 0) alineacion = num;
        }
        std::uint64_t maxOffset = 0;
        for (std::uint64_t i = 0; i < nTensores; ++i) {
            lector.saltarTexto();
            const auto nDims = lector.leer<std::uint32_t>();
            if (nDims > 8) throw std::runtime_error("tensor GGUF inválido");
            lector.saltar(static_cast<std::uint64_t>(nDims) * 8);
            (void)lector.leer<std::uint32_t>();  // tipo ggml
            maxOffset = std::max(maxOffset, lector.leer<std::uint64_t>());
        }
        const std::uint64_t fin = lector.posicion();
        const std::uint64_t inicioDatos = (fin + alineacion - 1) / alineacion * alineacion;
        if (tamano <= inicioDatos + maxOffset) {
            estado.completo = false;
            estado.detalle = "archivo incompleto (descarga cortada)";
        }
    } catch (const std::exception&) {
        // Si la propia cabecera está cortada, el archivo no sirve.
        estado.completo = false;
        estado.detalle = "archivo incompleto o dañado";
    }
    return estado;
}

}  // namespace

EstadoArchivoModelo inspeccionarArchivoModelo(const std::filesystem::path& ruta) {
    std::error_code ec;
    const auto tamano = std::filesystem::file_size(ruta, ec);
    if (ec) {
        EstadoArchivoModelo e;
        e.completo = false;
        e.detalle = "no se puede leer";
        return e;
    }
    const auto mtime = std::filesystem::last_write_time(ruta, ec);
    const std::string clave = ruta.string() + "|" + std::to_string(tamano) + "|" +
                              std::to_string(ec ? 0 : mtime.time_since_epoch().count());

    // Estáticos filtrados a propósito: se usan desde hilos desacoplados que
    // pueden seguir vivos durante la salida del proceso.
    static auto* mutexCache = new std::mutex();
    static auto* cache = new std::unordered_map<std::string, EstadoArchivoModelo>();
    {
        std::lock_guard<std::mutex> lock(*mutexCache);
        auto it = cache->find(clave);
        if (it != cache->end()) return it->second;
    }
    auto estado = inspeccionarSinCache(ruta, tamano);
    std::lock_guard<std::mutex> lock(*mutexCache);
    (*cache)[clave] = estado;
    return estado;
}

} // namespace funshi::agente::llm
