/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Documentos/AlmacenVectores.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace funshi::agente::documentos {

namespace fs = std::filesystem;

namespace {

constexpr char kMagia[4] = {'F', 'V', 'D', 'B'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxCadena = 64u * 1024 * 1024;

// Enteros en little-endian, independiente de la plataforma.
template <typename T>
void escribirEntero(std::ostream& out, T v) {
    unsigned char b[sizeof(T)];
    for (std::size_t i = 0; i < sizeof(T); ++i) b[i] = static_cast<unsigned char>((static_cast<std::uint64_t>(v) >> (8 * i)) & 0xFF);
    out.write(reinterpret_cast<const char*>(b), sizeof(T));
}

template <typename T>
bool leerEntero(std::istream& in, T& v) {
    unsigned char b[sizeof(T)];
    if (!in.read(reinterpret_cast<char*>(b), sizeof(T))) return false;
    std::uint64_t x = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) x |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    v = static_cast<T>(x);
    return true;
}

void escribirCadena(std::ostream& out, const std::string& s) {
    escribirEntero<std::uint32_t>(out, static_cast<std::uint32_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool leerCadena(std::istream& in, std::string& s) {
    std::uint32_t n = 0;
    if (!leerEntero(in, n) || n > kMaxCadena) return false;
    s.resize(n);
    return n == 0 || static_cast<bool>(in.read(&s[0], n));
}

void escribirFloat(std::ostream& out, float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    escribirEntero(out, bits);
}

bool leerFloat(std::istream& in, float& f) {
    std::uint32_t bits = 0;
    if (!leerEntero(in, bits)) return false;
    std::memcpy(&f, &bits, sizeof(f));
    return true;
}

}  // namespace

void normalizar(std::vector<float>& v) {
    double suma = 0;
    for (float x : v) suma += static_cast<double>(x) * x;
    if (suma <= 0) return;
    const double n = std::sqrt(suma);
    for (float& x : v) x = static_cast<float>(x / n);
}

bool AlmacenVectores::guardar(const fs::path& archivo) const {
    std::error_code ec;
    fs::create_directories(archivo.parent_path(), ec);
    const fs::path tmp = archivo.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(kMagia, 4);
        escribirEntero(out, kVersion);
        escribirCadena(out, modelo);
        escribirEntero<std::uint32_t>(out, static_cast<std::uint32_t>(dimension));
        escribirEntero<std::uint32_t>(out, static_cast<std::uint32_t>(documentos.size()));
        for (const auto& d : documentos) {
            escribirCadena(out, d.ruta);
            escribirEntero<std::int64_t>(out, d.mtime);
            escribirEntero<std::uint64_t>(out, d.tamano);
            escribirCadena(out, d.metodo);
            escribirCadena(out, d.error);
            escribirEntero<std::uint32_t>(out, static_cast<std::uint32_t>(d.fragmentos.size()));
            for (const auto& f : d.fragmentos) {
                escribirCadena(out, f.texto);
                escribirEntero<std::uint32_t>(out, static_cast<std::uint32_t>(std::max(0, f.pagina)));
                const bool tiene = dimension > 0 && static_cast<int>(f.vector.size()) == dimension;
                escribirEntero<std::uint8_t>(out, tiene ? 1 : 0);
                if (tiene) for (float x : f.vector) escribirFloat(out, x);
            }
        }
        if (!out) return false;
    }
    fs::rename(tmp, archivo, ec);
    return !ec;
}

bool AlmacenVectores::cargar(const fs::path& archivo, std::string* error) {
    const auto fallar = [&](const char* motivo) {
        if (error) *error = motivo;
        *this = AlmacenVectores{};
        return false;
    };
    std::ifstream in(archivo, std::ios::binary);
    if (!in) return fallar("no existe");
    char magia[4];
    std::uint32_t version = 0, dim = 0, nDocs = 0;
    if (!in.read(magia, 4) || std::memcmp(magia, kMagia, 4) != 0) return fallar("no es una base FVDB");
    if (!leerEntero(in, version) || version != kVersion) return fallar("versión de FVDB desconocida");
    AlmacenVectores a;
    if (!leerCadena(in, a.modelo) || !leerEntero(in, dim) || !leerEntero(in, nDocs)) return fallar("cabecera dañada");
    a.dimension = static_cast<int>(dim);
    for (std::uint32_t i = 0; i < nDocs; ++i) {
        DocumentoIndexado d;
        std::uint32_t nFrag = 0;
        if (!leerCadena(in, d.ruta) || !leerEntero(in, d.mtime) || !leerEntero(in, d.tamano) ||
            !leerCadena(in, d.metodo) || !leerCadena(in, d.error) || !leerEntero(in, nFrag)) {
            return fallar("documento dañado");
        }
        for (std::uint32_t j = 0; j < nFrag; ++j) {
            Fragmento f;
            std::uint32_t pagina = 0;
            std::uint8_t tiene = 0;
            if (!leerCadena(in, f.texto) || !leerEntero(in, pagina) || !leerEntero(in, tiene)) return fallar("fragmento dañado");
            f.pagina = static_cast<int>(pagina);
            if (tiene) {
                f.vector.resize(dim);
                for (auto& x : f.vector) {
                    if (!leerFloat(in, x)) return fallar("vector dañado");
                }
            }
            d.fragmentos.push_back(std::move(f));
        }
        a.documentos.push_back(std::move(d));
    }
    *this = std::move(a);
    return true;
}

DocumentoIndexado* AlmacenVectores::buscarDocumento(const std::string& ruta) {
    for (auto& d : documentos) {
        if (d.ruta == ruta) return &d;
    }
    return nullptr;
}

std::size_t AlmacenVectores::totalFragmentos() const {
    std::size_t n = 0;
    for (const auto& d : documentos) n += d.fragmentos.size();
    return n;
}

std::size_t AlmacenVectores::fragmentosConVector() const {
    std::size_t n = 0;
    for (const auto& d : documentos) {
        for (const auto& f : d.fragmentos) n += f.vector.empty() ? 0 : 1;
    }
    return n;
}

std::vector<Coincidencia> AlmacenVectores::masParecidos(const std::vector<float>& consulta, std::size_t k) const {
    std::vector<Coincidencia> todas;
    if (static_cast<int>(consulta.size()) != dimension || dimension == 0) return todas;
    for (const auto& d : documentos) {
        for (const auto& f : d.fragmentos) {
            if (f.vector.size() != consulta.size()) continue;
            double p = 0;
            for (std::size_t i = 0; i < consulta.size(); ++i) p += static_cast<double>(consulta[i]) * f.vector[i];
            todas.push_back({&d, &f, p});
        }
    }
    const std::size_t n = std::min(k, todas.size());
    std::partial_sort(todas.begin(), todas.begin() + static_cast<long>(n), todas.end(),
                      [](const Coincidencia& a, const Coincidencia& b) { return a.puntaje > b.puntaje; });
    todas.resize(n);
    return todas;
}

}  // namespace funshi::agente::documentos
