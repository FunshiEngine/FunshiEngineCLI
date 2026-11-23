/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Utilidades y grupos de prueba de cli-tests (ver CliTests.cpp).
#pragma once

#include <filesystem>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace pruebas_cli {

namespace fs = std::filesystem;

// Contadores globales de comprobaciones (definidos en PruebasCliComun.cpp).
extern int total;
extern int fallos;

}  // namespace pruebas_cli

#define CHECK(cond, msg) do { ++::pruebas_cli::total; if (!(cond)) { ++::pruebas_cli::fallos; std::cout << "FALLO: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; } } while (0)

namespace pruebas_cli {

// --- Ejecución del binario ---------------------------------------------------
int codigoSalidaSistema(int rc);
std::string ejecutarSistema(const std::string& cmd);
std::string leerArchivo(const fs::path& ruta);
nlohmann::json leerJson(const fs::path& ruta);
bool esRespuestaOk(const nlohmann::json& j);
std::string codigoError(const nlohmann::json& j);
nlohmann::json solicitudJson(const std::string& comando,
                             nlohmann::json args = nlohmann::json::object());
int ejecutarPeticion(const fs::path& bin, const fs::path& peticion, const fs::path& salida);
nlohmann::json ejecutarSolicitudJson(const fs::path& bin, const fs::path& tmp,
                                     const std::string& nombre,
                                     const nlohmann::json& solicitud, int& rc);
nlohmann::json ejecutarAislado(const fs::path& bin, const fs::path& trabajo,
                               const fs::path& tmp, const std::string& nombre,
                               const nlohmann::json& solicitud, int& rc);
void fijarVariable(const char* nombre, const std::string& valor);
fs::path rutaBinarioCLI(const char* argv0);

// Directorio de trabajo aislado: cada solicitud corre en un proceso nuevo
// con cwd = `trabajo`, igual que la CLI real.
struct EntornoAislado {
    fs::path bin;
    fs::path trabajo;
    fs::path tmp;
    nlohmann::json ejecutar(const std::string& nombre, const nlohmann::json& solicitud,
                            int& rc) const {
        return ejecutarAislado(bin, trabajo, tmp, nombre, solicitud, rc);
    }
};

// --- Grupos de prueba ---------------------------------------------------------
// PruebasCliBasicas.cpp
void probarComandosBasicos(const fs::path& bin, const fs::path& tmp);
void probarRecorridoProyecto(const fs::path& bin, const fs::path& tmp);
// PruebasCliAisladas.cpp
void probarConfiguracion(const EntornoAislado& env);
void probarEscenaYArchivos(const EntornoAislado& env);
void probarAgente(const EntornoAislado& env);

}  // namespace pruebas_cli
