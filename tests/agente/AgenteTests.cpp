/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// main de agente-tests. Corre en una carpeta temporal propia: la auditoría,
// las sesiones y la config de prueba no tocan las del usuario.
#include "PruebasAgente.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace pruebas_agente {
int total = 0;
int fallos = 0;
}  // namespace pruebas_agente

int main() {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / ("agente-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp / ".agentes");
    // Configuración y datos de proyectos en la carpeta temporal (antes de que
    // el motor resuelva y guarde en caché sus rutas).
#ifdef _WIN32
    _putenv_s("FUNSHI_CONFIG_DIR", (tmp / ".agentes").string().c_str());
    _putenv_s("FUNSHI_DATA_DIR", (tmp / "datos").string().c_str());
#else
    setenv("FUNSHI_CONFIG_DIR", (tmp / ".agentes").c_str(), 1);
    setenv("FUNSHI_DATA_DIR", (tmp / "datos").c_str(), 1);
#endif
    fs::current_path(tmp);

    pruebas_agente::probarConocimiento();
    pruebas_agente::probarHerramientas();
    pruebas_agente::probarSubagentes();
    pruebas_agente::probarSesiones();
    pruebas_agente::probarMonitor();
    pruebas_agente::probarPreferencias();
    pruebas_agente::probarUsoDeComandos();
    pruebas_agente::probarDocumentos();
    pruebas_agente::probarTareas();

    std::error_code ec;
    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(tmp, ec);
    std::cout << (pruebas_agente::fallos == 0 ? "OK" : "FALLOS") << ": " << pruebas_agente::total
              << " comprobaciones, " << pruebas_agente::fallos << " fallos\n";
    return pruebas_agente::fallos == 0 ? 0 : 1;
}
