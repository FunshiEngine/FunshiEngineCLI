/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Tui/TerminalStty.h"

#ifdef _WIN32

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace funshi {

namespace {

std::filesystem::path rutaCapturaStty() {
    return std::filesystem::temp_directory_path() /
           ("funshi_stty_" + std::to_string(GetCurrentProcessId()) + ".txt");
}

}  // namespace

// Ejecuta `stty` con los argumentos dados y devuelve lo que escribió (vacío
// si falló). La salida va a un archivo y stderr a NUL: estas órdenes se
// emiten antes de pintar y al salir, y no deben dejar texto en la pantalla.
// `stty` se busca en el PATH heredado, que en MSYS2 y Git Bash lo incluye.
std::string sttyHaciaArchivo(const std::string& argumentos) {
    const std::filesystem::path ruta = rutaCapturaStty();
    const std::string orden = "stty " + argumentos + " > \"" +
                              ruta.string() + "\" 2>nul";
    if (std::system(orden.c_str()) != 0) {
        std::filesystem::remove(ruta);
        return {};
    }
    std::string texto;
    {
        std::ifstream archivo(ruta);
        std::ostringstream flujo;
        flujo << archivo.rdbuf();
        texto = flujo.str();
    }
    std::filesystem::remove(ruta);
    while (!texto.empty() && (texto.back() == '\n' || texto.back() == '\r' ||
                              texto.back() == ' ' || texto.back() == '\t')) {
        texto.pop_back();
    }
    return texto;
}

// Guarda la configuración de la terminal para poder restaurarla al salir.
// `stty -g` emite grupos hexadecimales separados por dos puntos; cualquier
// otra salida significa que la orden no corrió sobre una terminal.
std::string guardarAjustesTerminal() {
    const std::string ajustes = sttyHaciaArchivo("-g");
    if (ajustes.empty() ||
        ajustes.find_first_not_of("0123456789abcdefABCDEF:") !=
            std::string::npos) {
        return {};
    }
    return ajustes;
}

// Modo crudo: cada tecla se entrega de inmediato, sin eco (para que no se
// escriba encima del dibujo) y sin que el terminal se reserve Ctrl+C ni
// Ctrl+S, que la TUI interpreta como teclas.
bool ponerTerminalEnCrudo() {
    return std::system(
               "stty -icanon min 1 time 0 -echo -isig -ixon 2>nul") == 0;
}

// Tamaño de la terminal en caracteres, o {0,0} si no se pudo averiguar.
ftxui::Dimensions detectarTamanoTerminal() {
    std::istringstream flujo(sttyHaciaArchivo("size"));
    int filas = 0;
    int columnas = 0;
    if (flujo >> filas >> columnas && filas > 0 && columnas > 0) {
        return {columnas, filas};
    }
    // Sin `stty` se recurre a lo que el entorno haya exportado.
    char buffer[32] = {};
    const DWORD columnasEnv =
        GetEnvironmentVariableA("COLUMNS", buffer, sizeof(buffer));
    const int ancho = columnasEnv > 0 && columnasEnv < sizeof(buffer)
                          ? std::atoi(buffer)
                          : 0;
    char bufferFilas[32] = {};
    const DWORD filasEnv =
        GetEnvironmentVariableA("LINES", bufferFilas, sizeof(bufferFilas));
    const int alto = filasEnv > 0 && filasEnv < sizeof(bufferFilas)
                         ? std::atoi(bufferFilas)
                         : 0;
    if (ancho > 0 && alto > 0) {
        return {ancho, alto};
    }
    return {0, 0};
}

// Devuelve la terminal a la configuración guardada al arrancar.
void restaurarTerminal(const std::string& ajustes) {
    if (!ajustes.empty()) {
        std::system(("stty " + ajustes + " 2>nul").c_str());
    }
}


}  // namespace funshi

#endif  // _WIN32
