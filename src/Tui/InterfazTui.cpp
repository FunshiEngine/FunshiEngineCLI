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
// Punto de entrada de la TUI: clasificación de terminal + arranque del bucle.
#include "Tui/InterfazTui.h"

#include "Tui/AplicacionTui.h"
#include "Tui/EntradaRespaldo.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Silvy.h"

#include "Nucleo/NucleoCLI.h"

#include <ftxui/component/screen_interactive.hpp>

namespace funshi {

std::string errorDeEntradaTui() {
    if (clasificarEntradaTui() != EntradaTui::SinTerminal) {
        return {};
    }
    return "La TUI necesita una terminal interactiva y stdin no lo es "
           "(¿redirigido desde un archivo o una tubería, o lanzado por un "
           "programa sin terminal?). Ejecuta `FunshiCLI --tui` directamente en "
           "una terminal.";
}

int ejecutarTui(int seccionInicial) {
    const EntradaTui entrada = clasificarEntradaTui();
    if (entrada == EntradaTui::SinTerminal) {
        // El motivo ya lo informó quien invocó la TUI; aquí solo se devuelve
        // el código de salida.
        return codigosSalida::kErrorUso;
    }
    ftxui::ScreenInteractive pantalla = ftxui::ScreenInteractive::Fullscreen();
    // Las instancias de Agente viven durante todo el bucle de la TUI y
    // Silvy las usa para ejecutar órdenes desde la sección Chat.
    Auditoria auditoria;
    Harness harness(auditoria);
    Silvy silvy(harness);
    auto aplicacion = crearAplicacionTui(&pantalla, auditoria, harness, silvy,
                                         seccionInicial);
    // En un pty sin consola FTXUI no lee teclas: se las entrega este lector,
    // que además detecta el tamaño de la terminal para el dibujo.
    EntradaRespaldo respaldo;
    if (entrada == EntradaTui::LecturaRespaldo) {
        respaldo.iniciar(&pantalla);
    }
    pantalla.Loop(aplicacion);
    return codigosSalida::kExito;
}

}  // namespace funshi
