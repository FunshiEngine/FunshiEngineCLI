/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — copia del texto seleccionado al portapapeles.
//
// La herramienta del sistema (wl-copy, xclip, pbcopy, tmux, Win32) corre en un
// hilo aparte y devuelve el resultado por `compartido_` con un evento Custom.
// Si no hay herramienta o falla, se usa OSC 52: la secuencia se escribe desde
// el hilo de la TUI, entre cuadros, para no mezclarse con el dibujado.
#include "Tui/AplicacionTui.h"

#include "Util/Clipboard.h"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace funshi {

// Hilo de la TUI (evento Custom): lanza la copia y recoge su resultado.
void AplicacionTui::procesarCopiaPendiente() {
    if (copiaPendiente_) {
        copiaPendiente_ = false;
        if (!textoSeleccionado_.empty()) {
            textoCopiado_ = textoSeleccionado_;
            lanzarCopia(textoSeleccionado_);
        }
    }

    bool lista = false, ok = false;
    std::string metodo, error, texto;
    {
        std::lock_guard<std::mutex> bloqueo(compartido_->mutex);
        if (compartido_->copiaLista) {
            lista = true;
            compartido_->copiaLista = false;
            ok = compartido_->copiaOk;
            metodo = std::move(compartido_->copiaMetodo);
            error = std::move(compartido_->copiaError);
            texto = std::move(compartido_->copiaTexto);
        }
    }
    if (!lista) return;

    // Cuenta caracteres (no bytes) y líneas para el aviso.
    std::size_t caracteres = 0;
    for (unsigned char c : texto) caracteres += (c & 0xC0) != 0x80;
    const auto lineas = 1 + std::count(texto.begin(), texto.end(), '\n');
    const std::string cuanto = (lineas > 1 ? std::to_string(lineas) + " líneas, " : "") +
                               std::to_string(caracteres) + " caracteres";

    if (ok) {
        mostrarToast("Copiado: " + cuanto + " (" + metodo + ")", false);
        return;
    }
    // Sin herramienta del sistema (o falló): OSC 52, la terminal copia.
    // Se escribe aquí, entre cuadros, para no mezclarse con el dibujado.
    const std::string osc = util::Clipboard::secuenciaOsc52(texto);
    if (!osc.empty()) {
        std::cout << osc << std::flush;
        mostrarToast("Enviado a la terminal: " + cuanto + " (OSC 52)", false);
    } else {
        mostrarToast("No se pudo copiar: " + (error.empty() ? "texto demasiado largo" : error), true);
    }
}

// Copia en segundo plano: xclip/wl-copy lanzan un proceso y no deben frenar
// la TUI. El resultado vuelve por `compartido_` con un evento Custom.
void AplicacionTui::lanzarCopia(const std::string& texto) {
    const auto compartido = compartido_;
    const auto publicar = [compartido](util::ResultadoCopia r, std::string t) {
        std::lock_guard<std::mutex> bloqueo(compartido->mutex);
        compartido->copiaLista = true;
        compartido->copiaOk = r.ok;
        compartido->copiaMetodo = std::move(r.metodo);
        compartido->copiaError = std::move(r.error);
        compartido->copiaTexto = std::move(t);
        if (compartido->pantalla) compartido->pantalla->PostEvent(ftxui::Event::Custom);
    };
    if (!util::Clipboard::hayHerramientaSistema()) {
        // Nada que lanzar: directo al respaldo OSC 52.
        publicar({false, "", "no hay herramienta de portapapeles"}, texto);
        return;
    }
    std::thread([publicar, texto]() { publicar(util::Clipboard::copiar(texto), texto); }).detach();
}

}  // namespace funshi
