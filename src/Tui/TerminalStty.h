/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Helpers de terminal para Windows (stty vía MSYS2/Git Bash) usados por EntradaRespaldo.
#ifndef FUNSHI_TUI_TERMINAL_STTY_H
#define FUNSHI_TUI_TERMINAL_STTY_H

#include <ftxui/screen/terminal.hpp>
#include <string>

namespace funshi {

#ifdef _WIN32

// Ejecuta `stty` y devuelve su salida (vacío si falló).
std::string sttyHaciaArchivo(const std::string& argumentos);

// Guarda la configuración de la terminal (`stty -g`) para restaurarla al salir.
std::string guardarAjustesTerminal();

// Pone la terminal en modo crudo (sin eco, sin icanon, sin isig/ixon).
bool ponerTerminalEnCrudo();

// Tamaño de la terminal en caracteres, o {0,0} si no se pudo averiguar.
ftxui::Dimensions detectarTamanoTerminal();

// Restaura la terminal a la configuración guardada.
void restaurarTerminal(const std::string& ajustes);

#endif  // _WIN32

}  // namespace funshi

#endif
