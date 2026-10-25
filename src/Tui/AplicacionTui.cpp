/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Fábrica de AplicacionTui.
#include "Tui/AplicacionTui.h"
#include "Tui/TemaTui.h"

#include <memory>

namespace funshi {

ftxui::Component crearAplicacionTui(ftxui::ScreenInteractive* pantalla,
                                    Auditoria& auditoria,
                                    Harness& harness,
                                    Silvy& silvy,
                                    int seccionInicial) {
    auto aplicacion = std::make_shared<AplicacionTui>(pantalla, auditoria, harness, silvy);
    if (seccionInicial >= 0 && seccionInicial < kCantidadSecciones) {
        aplicacion->irASeccion(seccionInicial);
    }
    return aplicacion;
}

}  // namespace funshi
