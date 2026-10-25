/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Lectura de teclas de la TUI para terminales sin consola de Windows.
#include "Tui/EntradaRespaldo.h"
#include "Tui/TerminalStty.h"

#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>
#include <ftxui/component/terminal_input_parser.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace funshi {

EntradaTui clasificarEntradaTui() {
#ifdef _WIN32
    const HANDLE entrada = GetStdHandle(STD_INPUT_HANDLE);
    if (entrada == nullptr || entrada == INVALID_HANDLE_VALUE) {
        return EntradaTui::SinTerminal;
    }
    DWORD modo = 0;
    if (GetConsoleMode(entrada, &modo)) {
        return EntradaTui::FtxuiDirecto;
    }
    const DWORD tipo = GetFileType(entrada);
    if (tipo == FILE_TYPE_PIPE || tipo == FILE_TYPE_CHAR) {
        return detectarTamanoTerminal().dimx > 0 ? EntradaTui::LecturaRespaldo
                                                 : EntradaTui::SinTerminal;
    }
    return EntradaTui::SinTerminal;
#else
    return EntradaTui::FtxuiDirecto;
#endif
}

struct EntradaRespaldo::Impl {
    ftxui::ScreenInteractive* pantalla = nullptr;
    std::string ajustesGuardados;
    bool terminalModificado = false;
    std::mutex mutex;
    std::condition_variable llego;
    std::string buffer;
    bool activo = true;
    bool finEntrada = false;
    std::thread hiloConsumidor;

    static void lector(const std::shared_ptr<Impl>& impl);
    static void consumidor(const std::shared_ptr<Impl>& impl);
};

void EntradaRespaldo::Impl::lector(const std::shared_ptr<Impl>& impl) {
    for (;;) {
        char datos[512];
        std::size_t leidos = 0;
#ifdef _WIN32
        DWORD obtenidos = 0;
        const BOOL ok = ReadFile(GetStdHandle(STD_INPUT_HANDLE), datos,
                                 static_cast<DWORD>(sizeof(datos)), &obtenidos,
                                 nullptr);
        leidos = ok ? static_cast<std::size_t>(obtenidos) : 0;
#else
        const ssize_t obtenidos = ::read(STDIN_FILENO, datos, sizeof(datos));
        leidos = obtenidos > 0 ? static_cast<std::size_t>(obtenidos) : 0;
#endif
        {
            std::lock_guard<std::mutex> bloqueo(impl->mutex);
            if (leidos == 0) {
                impl->finEntrada = true;
            } else {
                impl->buffer.append(datos, leidos);
            }
        }
        impl->llego.notify_all();
        if (leidos == 0) {
            return;
        }
    }
}

void EntradaRespaldo::Impl::consumidor(const std::shared_ptr<Impl>& impl) {
    ftxui::TerminalInputParser parser([impl](ftxui::Event evento) {
        impl->pantalla->PostEvent(std::move(evento));
    });

    auto anterior = std::chrono::steady_clock::now();
    for (;;) {
        std::string bytes;
        {
            std::unique_lock<std::mutex> bloqueo(impl->mutex);
            impl->llego.wait_for(bloqueo, std::chrono::milliseconds(50),
                                 [impl] {
                                     return !impl->activo ||
                                            !impl->buffer.empty();
                                 });
            const bool activo = impl->activo;
            const bool sinEntrada = impl->finEntrada;
            bytes.swap(impl->buffer);
            bloqueo.unlock();
            if (!activo) {
                return;
            }
            if (sinEntrada) {
                impl->pantalla->Exit();
                return;
            }
        }

        const auto ahora = std::chrono::steady_clock::now();
        const int transcurrido = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(ahora -
                                                                  anterior)
                .count());
        anterior = ahora;
        parser.Timeout(transcurrido);
        for (const char byte : bytes) {
            parser.Add(byte);
        }
    }
}

EntradaRespaldo::~EntradaRespaldo() {
    if (!impl_) {
        return;
    }
    {
        std::lock_guard<std::mutex> bloqueo(impl_->mutex);
        impl_->activo = false;
    }
    impl_->llego.notify_all();
    if (impl_->hiloConsumidor.joinable()) {
        impl_->hiloConsumidor.join();
    }
    if (impl_->terminalModificado) {
#ifdef _WIN32
        restaurarTerminal(impl_->ajustesGuardados);
#endif
    }
}

void EntradaRespaldo::iniciar(ftxui::ScreenInteractive* pantalla) {
    impl_ = std::make_shared<Impl>();
    impl_->pantalla = pantalla;
    pantalla->TrackMouse(false);

#ifdef _WIN32
    const ftxui::Dimensions tamano = detectarTamanoTerminal();
    if (tamano.dimx > 0 && tamano.dimy > 0) {
        ftxui::Terminal::SetFallbackSize(tamano);
    }
    impl_->ajustesGuardados = guardarAjustesTerminal();
    impl_->terminalModificado =
        !impl_->ajustesGuardados.empty() && ponerTerminalEnCrudo();
#endif

    impl_->hiloConsumidor =
        std::thread(&EntradaRespaldo::Impl::consumidor, impl_);
    std::thread(&EntradaRespaldo::Impl::lector, impl_).detach();
}

}  // namespace funshi
