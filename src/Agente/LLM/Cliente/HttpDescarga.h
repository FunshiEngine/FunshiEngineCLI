/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Utilidades HTTP compartidas (cURL): GET con timeout y descarga de archivos
// grandes con progreso real, reanudación y detección de falta de conexión.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace funshi::agente::llm::http {

// Resultado de una operación HTTP. `sinConexion` distingue la falta de red
// (DNS, conexión rechazada, timeout de conexión) de otros fallos.
struct ResultadoHttp {
    bool ok = false;
    bool sinConexion = false;
    long codigoHttp = 0;
    std::string error;
};

// Progreso de descarga: bytes recibidos y total esperado (0 = desconocido).
using ProgresoBytes = std::function<void(std::uint64_t recibidos, std::uint64_t total)>;

// Verificación opcional del archivo descargado antes de moverlo a su destino.
// Devuelve false (y rellena el error) para descartar la descarga.
using Verificador = std::function<bool(const std::filesystem::path& archivo, std::string& error)>;

struct OpcionesDescarga {
    std::vector<std::string> cabeceras;  // "Nombre: valor"
    std::string userAgent = "FunshiEngineCLI/1.0";
    std::uint64_t tamanoEsperado = 0;     // 0 = desconocido
    long timeoutConexionMs = 15000;
    Verificador verificar;
};

// Inicializa cURL para todo el proceso (idempotente, seguro entre hilos).
void inicializarCurl();

// GET en memoria.
ResultadoHttp get(const std::string& url, std::string& respuesta,
                  const std::vector<std::string>& cabeceras,
                  const std::string& userAgent, long timeoutMs);

// Descarga `url` a `destino`. Escribe en `destino + ".part"` (reanudando si
// ya existe) y solo al completar y verificar reemplaza `destino`. Si falla, el
// archivo final existente nunca se toca y el `.part` queda para reanudar.
ResultadoHttp descargar(const std::string& url, const std::filesystem::path& destino,
                        const OpcionesDescarga& opciones, const ProgresoBytes& progreso);

// Ruta del archivo parcial asociado a un destino.
std::filesystem::path rutaParcial(const std::filesystem::path& destino);

// Texto legible para bytes ("512.3 MB").
std::string formatearBytes(std::uint64_t bytes);

// Mensaje estándar para la falta de conexión.
std::string mensajeSinConexion();

}  // namespace funshi::agente::llm::http
