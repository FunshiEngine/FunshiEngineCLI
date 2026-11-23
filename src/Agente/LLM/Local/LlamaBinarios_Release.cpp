/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// LlamaBinarios: consulta de releases en GitHub e instalación del binario.
#include "Agente/LLM/Local/LlamaBinariosInterno.h"

#include "Agente/LLM/Cliente/HttpDescarga.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <regex>

namespace funshi::agente::llm::detalle_llama {

namespace {

namespace fs = std::filesystem;

const char* kApiReleases =
    "https://api.github.com/repos/ggml-org/llama.cpp/releases?per_page=30";

#ifdef _WIN32
const char* kNombreServidor = "llama-server.exe";
#else
const char* kNombreServidor = "llama-server";
#endif

// Busca llama-server dentro del árbol extraído.
fs::path buscarServidor(const fs::path& raiz) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(raiz, ec), fin; !ec && it != fin; it.increment(ec)) {
        if (it->path().filename() == kNombreServidor) return it->path();
    }
    return {};
}

std::string comillas(const fs::path& p) {
    return "\"" + p.string() + "\"";
}

ResultadoPaso fallo(std::string error, bool sinConexion = false) {
    return {false, sinConexion, std::move(error)};
}

// Extrae `archivo` y deja el árbol en `destino`; devuelve la ruta relativa
// de llama-server dentro de él.
ResultadoPaso extraer(const fs::path& dirBin, const fs::path& archivo, const ReleaseLlama& release,
                      fs::path& servidor) {
    std::error_code ec;
    const fs::path temporal = dirBin / (release.tag + ".tmp");
    const fs::path destino = dirBin / release.tag;
    fs::remove_all(temporal, ec);
    fs::create_directories(temporal, ec);
    // `tar` extrae .tar.gz en Linux/macOS y .zip en Windows 10+ (bsdtar).
#ifdef _WIN32
    const char* silencio = " >NUL 2>&1";
#else
    const char* silencio = " >/dev/null 2>&1";  // no ensuciar la TUI
#endif
    const std::string cmd = "tar -xf " + comillas(archivo) + " -C " + comillas(temporal) + silencio;
    const int rc = std::system(cmd.c_str());
    const fs::path servidorTmp = buscarServidor(temporal);
    if (rc != 0 || servidorTmp.empty()) {
        fs::remove_all(temporal, ec);
        fs::remove(archivo, ec);
        return fallo("no se pudo extraer " + release.asset);
    }
    const fs::path relativo = fs::relative(servidorTmp, temporal, ec);
    fs::remove_all(destino, ec);
    fs::rename(temporal, destino, ec);
    if (ec) {
        fs::remove_all(temporal, ec);
        return fallo("no se pudo instalar en " + destino.string());
    }
    servidor = destino / relativo;
    fs::permissions(servidor, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    return {true, false, {}};
}

// Registra la instalación (escritura atómica) y borra versiones anteriores.
void registrar(const fs::path& dirBin, const fs::path& archivo, const ReleaseLlama& release,
               const PlataformaLlama& plataforma, const fs::path& servidor) {
    std::error_code ec;
    nlohmann::json registro = {
        {"tag", release.tag},
        {"asset", release.asset},
        {"server", servidor.string()},
        {"platform", plataforma.so + "-" + plataforma.arquitectura},
    };
    const fs::path regTmp = archivoRegistro(dirBin).string() + ".tmp";
    {
        std::ofstream out(regTmp);
        out << registro.dump(2);
    }
    fs::rename(regTmp, archivoRegistro(dirBin), ec);
    fs::remove(archivo, ec);
    for (fs::directory_iterator it(dirBin, ec), fin; !ec && it != fin; it.increment(ec)) {
        std::error_code ec2;
        if (it->is_directory(ec2) && it->path().filename() != release.tag) {
            fs::remove_all(it->path(), ec2);  // en Windows puede fallar si está en uso
        }
    }
}

}  // namespace

fs::path archivoRegistro(const fs::path& dirBin) { return dirBin / "installed.json"; }

ResultadoPaso buscarUltimaRelease(const PlataformaLlama& plataforma, ReleaseLlama& release) {
    std::string respuesta;
    const auto r = http::get(kApiReleases, respuesta, {"Accept: application/vnd.github+json"},
                             "FunshiEngineCLI/1.0", 15000);
    if (!r.ok) {
        return fallo(r.sinConexion ? "sin conexión, no se pudo buscar actualizaciones"
                                   : "no se pudo consultar GitHub: " + r.error,
                     r.sinConexion);
    }
    const nlohmann::json releases = nlohmann::json::parse(respuesta, nullptr, false);
    // llama.cpp publica cada build (bNNNN) como release; se toma la más
    // reciente no-borrador que tenga binario para esta plataforma.
    const std::regex patronTag("^b[0-9]+$");
    if (releases.is_array()) {
        for (const auto& rel : releases) {
            if (!rel.is_object() || rel.value("draft", false)) continue;
            const std::string tag = rel.value("tag_name", std::string{});
            if (!std::regex_match(tag, patronTag)) continue;
            const std::string esperado = "llama-" + tag + "-" + plataforma.sufijoAsset;
            for (const auto& a : rel.value("assets", nlohmann::json::array())) {
                if (a.value("name", std::string{}) == esperado) {
                    release.tag = tag;
                    release.asset = esperado;
                    release.url = a.value("browser_download_url", std::string{});
                    release.tamano = a.value("size", 0ULL);
                    return release.url.empty()
                               ? fallo("no se encontró un binario para " + plataforma.sufijoAsset)
                               : ResultadoPaso{true, false, {}};
                }
            }
        }
    }
    return fallo("no se encontró un binario para " + plataforma.sufijoAsset);
}

ResultadoPaso instalarRelease(const fs::path& dirBin, const ReleaseLlama& release,
                              const PlataformaLlama& plataforma, const std::string& accion,
                              const InformarProgreso& informar, fs::path& servidor) {
    std::error_code ec;
    fs::create_directories(dirBin, ec);
    const fs::path archivo = dirBin / release.asset;
    http::OpcionesDescarga opciones;
    opciones.tamanoEsperado = release.tamano;
    const auto rd = http::descargar(
        release.url, archivo, opciones, [&](std::uint64_t rec, std::uint64_t total) {
            if (total > 0) {
                informar(static_cast<double>(rec) / static_cast<double>(total),
                         accion + ": " + http::formatearBytes(rec) + " / " +
                             http::formatearBytes(total));
            } else {
                informar(-1.0, accion + ": " + http::formatearBytes(rec));
            }
        });
    if (!rd.ok) {
        return fallo(rd.sinConexion ? "sin conexión, no se pudo actualizar"
                                    : "falló la descarga: " + rd.error,
                     rd.sinConexion);
    }

    informar(-1.0, "Extrayendo " + release.asset + "...");
    const auto rx = extraer(dirBin, archivo, release, servidor);
    if (!rx.ok) return rx;
    registrar(dirBin, archivo, release, plataforma, servidor);
    return {true, false, {}};
}

}  // namespace funshi::agente::llm::detalle_llama
