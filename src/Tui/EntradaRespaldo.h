/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    SPDX-License-Identifier: Apache-2.0
*/
// Entrada de la TUI según la terminal donde se la lanza.
//
// FTXUI en Windows lee las teclas con la API de consola (GetConsoleMode y
// ReadConsoleInput). Esa vía sirve en cmd, PowerShell, Windows Terminal y en
// mintty con ConPTY, pero no en un pty de MSYS2 o Git Bash: ahí el stdin del
// proceso no es una consola (el runtime de MSYS2 lo implementa sobre pipes de
// Windows), GetConsoleMode falla con ERROR_INVALID_HANDLE y la TUI se dibuja
// sin recibir ni una tecla. Este
// módulo detecta ese caso y lo cubre leyendo stdin a mano (con `stty` para no
// esperar la Intro) y traduciendo las teclas a eventos de FTXUI con el mismo
// parser que usa la librería, de modo que ambas rutas emiten eventos idénticos.
#ifndef FUNSHI_TUI_ENTRADA_RESPALDO_H
#define FUNSHI_TUI_ENTRADA_RESPALDO_H

#include <memory>

#include <ftxui/component/screen_interactive.hpp>

namespace funshi {

// Dónde debe la TUI leer las teclas en la terminal actual.
enum class EntradaTui {
    // Consola de Windows nativa o la vía POSIX: FTXUI lee la entrada por su
    // cuenta y no hace falta nada más.
    FtxuiDirecto,
    // Pty sin consola nativa (mintty/MSYS2, Git Bash): la lectura se hace en
    // este módulo y se traduce a eventos de FTXUI.
    LecturaRespaldo,
    // stdin es una tubería o un archivo: no hay terminal interactiva y la TUI
    // no recibiría ninguna tecla.
    SinTerminal,
};

// Clasifica la terminal actual a partir del manejador de stdin del proceso.
EntradaTui clasificarEntradaTui();

// Lector de teclas sobre stdin para terminales sin consola de Windows. Es
// RAII: `iniciar()` arranca la lectura y el destructor la detiene y devuelve
// la terminal a su configuración original.
class EntradaRespaldo {
 public:
    EntradaRespaldo() = default;
    ~EntradaRespaldo();

    EntradaRespaldo(const EntradaRespaldo&) = delete;
    EntradaRespaldo& operator=(const EntradaRespaldo&) = delete;

    // Pone la terminal en modo crudo (cada tecla llega sin esperar Intro y sin
    // eco sobre el dibujo), fija el tamaño detectado como reserva de FTXUI y
    // arranca la lectura que empuja eventos a `pantalla`. Debe llamarse antes
    // de `ScreenInteractive::Loop()`.
    void iniciar(ftxui::ScreenInteractive* pantalla);

 private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace funshi

#endif  // FUNSHI_TUI_ENTRADA_RESPALDO_H
