/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Cliente/HttpDescarga.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace funshi::agente::llm::http {

namespace {

namespace fs = std::filesystem;

bool esErrorDeRed(CURLcode codigo) {
    switch (codigo) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PARTIAL_FILE:
            return true;
        default:
            return false;
    }
}

size_t escribirMemoria(void* datos, size_t tam, size_t n, void* usuario) {
    auto* salida = static_cast<std::string*>(usuario);
    salida->append(static_cast<char*>(datos), tam * n);
    return tam * n;
}

size_t escribirArchivo(void* datos, size_t tam, size_t n, void* usuario) {
    return std::fwrite(datos, tam, n, static_cast<std::FILE*>(usuario));
}

struct EstadoProgreso {
    const ProgresoBytes* callback = nullptr;
    std::uint64_t desplazamiento = 0;  // bytes ya presentes al reanudar
    std::uint64_t totalConocido = 0;
    std::chrono::steady_clock::time_point ultimo{};
};

int progresoCurl(void* usuario, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* estado = static_cast<EstadoProgreso*>(usuario);
    if (!estado || !estado->callback || !*estado->callback) return 0;
    const auto ahora = std::chrono::steady_clock::now();
    // Limitar a ~8 actualizaciones por segundo para no saturar la UI.
    if (ahora - estado->ultimo < std::chrono::milliseconds(125) && dlnow != dltotal) return 0;
    estado->ultimo = ahora;
    const std::uint64_t recibidos = estado->desplazamiento + static_cast<std::uint64_t>(dlnow);
    std::uint64_t total = estado->totalConocido;
    if (dltotal > 0) total = estado->desplazamiento + static_cast<std::uint64_t>(dltotal);
    (*estado->callback)(recibidos, total);
    return 0;
}

curl_slist* armarCabeceras(const std::vector<std::string>& cabeceras, const std::string& userAgent) {
    curl_slist* lista = nullptr;
    for (const auto& c : cabeceras) lista = curl_slist_append(lista, c.c_str());
    lista = curl_slist_append(lista, ("User-Agent: " + userAgent).c_str());
    return lista;
}

std::uint64_t tamanoArchivo(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return 0;
    const auto t = fs::file_size(p, ec);
    return ec ? 0 : static_cast<std::uint64_t>(t);
}

}  // namespace

void inicializarCurl() {
    static std::once_flag bandera;
    std::call_once(bandera, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::string mensajeSinConexion() {
    return "Sin conexión a internet (o el servidor no responde). Revisa la red e inténtalo de nuevo.";
}

std::string formatearBytes(std::uint64_t bytes) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    const double b = static_cast<double>(bytes);
    if (b >= 1024.0 * 1024.0 * 1024.0) oss << b / (1024.0 * 1024.0 * 1024.0) << " GB";
    else if (b >= 1024.0 * 1024.0) oss << b / (1024.0 * 1024.0) << " MB";
    else if (b >= 1024.0) oss << b / 1024.0 << " KB";
    else oss << bytes << " B";
    return oss.str();
}

fs::path rutaParcial(const fs::path& destino) {
    fs::path p = destino;
    p += ".part";
    return p;
}

ResultadoHttp get(const std::string& url, std::string& respuesta,
                  const std::vector<std::string>& cabeceras,
                  const std::string& userAgent, long timeoutMs) {
    inicializarCurl();
    ResultadoHttp r;
    respuesta.clear();
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "No se pudo inicializar cURL";
        return r;
    }
    curl_slist* lista = armarCabeceras(cabeceras, userAgent);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, escribirMemoria);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respuesta);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, lista);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min(timeoutMs, 10000L));
    // Imprescindible en programas con hilos: sin esto, los timeouts de DNS
    // usan señales y pueden tumbar el proceso.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.codigoHttp);
    curl_slist_free_all(lista);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        r.sinConexion = esErrorDeRed(res);
        r.error = r.sinConexion ? mensajeSinConexion()
                                : "cURL: " + std::string(curl_easy_strerror(res));
        return r;
    }
    if (r.codigoHttp < 200 || r.codigoHttp >= 300) {
        r.error = "HTTP " + std::to_string(r.codigoHttp);
        return r;
    }
    r.ok = true;
    return r;
}

ResultadoHttp post(const std::string& url, const std::string& cuerpo, std::string& respuesta, long timeoutMs) {
    inicializarCurl();
    ResultadoHttp r;
    respuesta.clear();
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "No se pudo inicializar cURL";
        return r;
    }
    curl_slist* lista = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, cuerpo.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(cuerpo.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, escribirMemoria);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respuesta);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, lista);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min(timeoutMs, 10000L));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    const CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.codigoHttp);
    curl_slist_free_all(lista);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        r.sinConexion = esErrorDeRed(res);
        r.error = r.sinConexion ? mensajeSinConexion() : "cURL: " + std::string(curl_easy_strerror(res));
        return r;
    }
    if (r.codigoHttp < 200 || r.codigoHttp >= 300) {
        r.error = "HTTP " + std::to_string(r.codigoHttp);
        return r;
    }
    r.ok = true;
    return r;
}

ResultadoHttp descargar(const std::string& url, const fs::path& destino,
                        const OpcionesDescarga& opciones, const ProgresoBytes& progreso) {
    inicializarCurl();
    ResultadoHttp r;
    std::error_code ec;
    if (destino.has_parent_path()) fs::create_directories(destino.parent_path(), ec);

    const fs::path parcial = rutaParcial(destino);
    const std::uint64_t esperado = opciones.tamanoEsperado;

    // Un parcial más grande de lo esperado no sirve para reanudar.
    std::uint64_t yaDescargado = tamanoArchivo(parcial);
    if (esperado > 0 && yaDescargado > esperado) {
        fs::remove(parcial, ec);
        yaDescargado = 0;
    }

    for (int intento = 0; intento < 2; ++intento) {
        const bool completo = esperado > 0 && yaDescargado == esperado;
        if (!completo) {
            std::FILE* fp = std::fopen(parcial.string().c_str(), yaDescargado > 0 ? "ab" : "wb");
            if (!fp) {
                r.error = "No se pudo escribir " + parcial.string();
                return r;
            }
            CURL* curl = curl_easy_init();
            if (!curl) {
                std::fclose(fp);
                r.error = "No se pudo inicializar cURL";
                return r;
            }
            EstadoProgreso estado;
            estado.callback = &progreso;
            estado.desplazamiento = yaDescargado;
            estado.totalConocido = esperado;

            curl_slist* lista = armarCabeceras(opciones.cabeceras, opciones.userAgent);
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, escribirArchivo);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, lista);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, opciones.timeoutConexionMs);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 0L);  // descargas largas: sin límite total
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);  // aborta si < 1 KB/s...
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);     // ...durante 60 s
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            if (yaDescargado > 0) {
                curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                                 static_cast<curl_off_t>(yaDescargado));
            }
            // XFERINFO usa curl_off_t (la antigua PROGRESSFUNCTION usa double).
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progresoCurl);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &estado);

            const CURLcode res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.codigoHttp);
            curl_slist_free_all(lista);
            curl_easy_cleanup(curl);
            std::fclose(fp);

            if (res == CURLE_RANGE_ERROR || r.codigoHttp == 416) {
                // El servidor no acepta reanudar desde ese punto: empezar de cero.
                fs::remove(parcial, ec);
                yaDescargado = 0;
                continue;
            }
            if (res != CURLE_OK) {
                r.sinConexion = esErrorDeRed(res);
                if (r.sinConexion) {
                    r.error = mensajeSinConexion() + " Lo descargado se conserva y se reanudará.";
                } else if (r.codigoHttp == 401 || r.codigoHttp == 403) {
                    r.error = "Acceso denegado (HTTP " + std::to_string(r.codigoHttp) +
                              "). Puede requerir token o aceptar la licencia del modelo.";
                } else if (r.codigoHttp >= 400) {
                    r.error = "HTTP " + std::to_string(r.codigoHttp);
                } else {
                    r.error = "cURL: " + std::string(curl_easy_strerror(res));
                }
                return r;
            }
        }

        const std::uint64_t final = tamanoArchivo(parcial);
        if (esperado > 0 && final != esperado) {
            if (final > esperado) {
                fs::remove(parcial, ec);
                r.error = "Tamaño descargado inesperado; se descartó la descarga.";
            } else {
                r.error = "Descarga incompleta (" + formatearBytes(final) + " de " +
                          formatearBytes(esperado) + "); se reanudará en el próximo intento.";
                r.sinConexion = true;
            }
            return r;
        }
        if (opciones.verificar) {
            std::string errorVerif;
            if (!opciones.verificar(parcial, errorVerif)) {
                fs::remove(parcial, ec);
                r.error = errorVerif.empty() ? "La verificación del archivo falló" : errorVerif;
                return r;
            }
        }
        // Reemplazo atómico del destino solo con la descarga completa.
        fs::rename(parcial, destino, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(parcial, destino, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                r.error = "No se pudo mover la descarga a " + destino.string() + ": " + ec.message();
                return r;
            }
            fs::remove(parcial, ec);
        }
        if (progreso) progreso(tamanoArchivo(destino), tamanoArchivo(destino));
        r.ok = true;
        return r;
    }
    r.error = "El servidor rechazó reanudar la descarga";
    return r;
}

}  // namespace funshi::agente::llm::http
