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
// Lectura de teclas de la TUI para terminales sin consola de Windows. El
// módulo resuelve tres cosas: cómo detectar que FTXUI no va a poder leer la
// entrada, cómo poner la terminal en modo crudo sin romperla (y restaurarla al
// salir) y cómo traducir los bytes de stdin a eventos de FTXUI.
#include "Tui/EntradaRespaldo.h"

#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>
// El parser de secuencias de terminal de FTXUI es interno a la librería (vive
// en su árbol de fuentes, no en include/), pero es el que usa la vía de
// consola: reutilizarlo garantiza que las dos rutas produzcan los mismos
// eventos. El CMake agrega ese directorio para poder encontrarlo.
#include <ftxui/component/terminal_input_parser.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace funshi {

namespace {

#ifdef _WIN32

// Ruta del archivo temporal donde capturar la salida de `stty`. Lleva el pid
// de la sesión para que dos ejecuciones simultáneas no se pisen.
std::filesystem::path rutaCapturaStty() {
    return std::filesystem::temp_directory_path() /
           ("funshi_stty_" + std::to_string(GetCurrentProcessId()) + ".txt");
}

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

#endif  // _WIN32

}  // namespace

EntradaTui clasificarEntradaTui() {
#ifdef _WIN32
    const HANDLE entrada = GetStdHandle(STD_INPUT_HANDLE);
    if (entrada == nullptr || entrada == INVALID_HANDLE_VALUE) {
        return EntradaTui::SinTerminal;
    }
    DWORD modo = 0;
    if (GetConsoleMode(entrada, &modo)) {
        // Consola nativa (cmd, PowerShell, Windows Terminal o mintty con
        // ConPTY): FTXUI lee las teclas con la API de consola.
        return EntradaTui::FtxuiDirecto;
    }
    // Sin consola, los tipos que pueden esconder una terminal son el de
    // tubería y el de caracteres: un pty de MSYS2 o Git Bash el runtime lo
    // implementa con pipes de Windows, y un dispositivo como NUL se presenta
    // como de caracteres. `stty size` solo responde sobre una terminal real,
    // así que confirma cuál de los dos es; en una tubería o un archivo sin
    // terminal falla y no queda entrada posible para la TUI.
    const DWORD tipo = GetFileType(entrada);
    if (tipo == FILE_TYPE_PIPE || tipo == FILE_TYPE_CHAR) {
        return detectarTamanoTerminal().dimx > 0 ? EntradaTui::LecturaRespaldo
                                                 : EntradaTui::SinTerminal;
    }
    return EntradaTui::SinTerminal;
#else
    // En POSIX FTXUI abre la terminal por su cuenta (/dev/tty) o lee stdin
    // con su propio parser, así que nunca hace falta lectura de respaldo.
    return EntradaTui::FtxuiDirecto;
#endif
}

struct EntradaRespaldo::Impl {
    // La interfaz solo se toca desde el hilo consumidor, que vive hasta que
    // el destructor la detiene.
    ftxui::ScreenInteractive* pantalla = nullptr;

    // Estado de la terminal, para restaurarla al salir.
    std::string ajustesGuardados;
    bool terminalModificado = false;

    // Bytes leídos de stdin aún no traducidos.
    std::mutex mutex;
    std::condition_variable llego;
    std::string buffer;
    bool activo = true;
    bool finEntrada = false;

    std::thread hiloConsumidor;

    // Hilo que bloquea leyendo stdin y acumula los bytes en `buffer`. Sale
    // cuando la entrada se cierra (fin de archivo o error). Una lectura
    // bloqueante no se puede interrumpir, así que este hilo se suelta y el
    // estado que usa vive hasta que el proceso termine; no toca `pantalla`.
    static void lector(const std::shared_ptr<Impl>& impl);

    // Hilo que traduce los bytes acumulados a eventos y los encola en el
    // bucle principal de la interfaz (PostEvent está documentado como seguro
    // entre hilos). Si la entrada se cierra, se cierra la TUI: sin teclas no
    // habría forma de salir.
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
            // 50 ms es el umbral que el parser usa para dar por terminada una
            // secuencia de escape incompleta (Esc suelto), así que la espera
            // máxima de cada vuelta es ese mismo valor.
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

    // En esta vía no se traducen secuencias de puntero: el ratón queda fuera.
    pantalla->TrackMouse(false);

#ifdef _WIN32
    // Sin consola, FTXUI no puede consultar el tamaño de la terminal y usaría
    // su valor de reserva (80x24); el real se detecta una vez al arrancar.
    const ftxui::Dimensions tamano = detectarTamanoTerminal();
    if (tamano.dimx > 0 && tamano.dimy > 0) {
        ftxui::Terminal::SetFallbackSize(tamano);
    }

    // Las órdenes `stty` se ejecutan como hijos que heredan stdin; un hijo de
    // consola que arranca con la entrada encolada puede consumirla, así que
    // las teclas pulsadas durante este arranque se pierden. La clasificación
    // de la entrada también lanza un `stty`, de modo que la ventana va desde
    // el inicio del proceso hasta este punto.
    impl_->ajustesGuardados = guardarAjustesTerminal();
    impl_->terminalModificado =
        !impl_->ajustesGuardados.empty() && ponerTerminalEnCrudo();
#endif

    impl_->hiloConsumidor =
        std::thread(&EntradaRespaldo::Impl::consumidor, impl_);
    std::thread(&EntradaRespaldo::Impl::lector, impl_).detach();
}

}  // namespace funshi
