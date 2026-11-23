/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — aviso flotante (toast) arriba a la derecha. Se dibuja fuera
// del envoltorio de selección, así nunca entra en el texto copiado.
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/TemaTui.h"

#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <string>

namespace funshi {

void AplicacionTui::mostrarToast(const std::string& mensaje, bool error) {
    toastMensaje_ = mensaje;
    toastError_ = error;
    toastVisible_ = true;
    toastInicio_ = std::chrono::steady_clock::now();
    if (pantalla_) pantalla_->PostEvent(ftxui::Event());
    // Redibuja al vencer el tiempo para que el toast desaparezca solo.
    redibujarTras(kToastDuracionMs + 50);
}

ftxui::Element AplicacionTui::renderToast() {
    if (!toastVisible_) return ftxui::emptyElement();

    const auto transcurrido = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - toastInicio_)
                                  .count();
    if (transcurrido >= kToastDuracionMs) {
        toastVisible_ = false;
        return ftxui::emptyElement();
    }

    const ftxui::Color color = toastError_ ? kError : kExito;
    ftxui::Element toast = ftxui::text(std::string(toastError_ ? " ✗ " : " ✓ ") + toastMensaje_ + " ") |
                           ftxui::bold | ftxui::color(color) | ftxui::borderStyled(color) |
                           ftxui::bgcolor(ftxui::Color::RGB(30, 30, 30));
    return ftxui::vbox({
        ftxui::hbox({ftxui::filler(), std::move(toast)}),
        ftxui::filler(),
    });
}

}  // namespace funshi
