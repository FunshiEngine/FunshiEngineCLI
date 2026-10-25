/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — módulo raton: selección resaltada, copia automática y toast.
//
// Funcionamiento: el arrastre del ratón solo guarda coordenadas de pantalla.
// Un nodo envoltorio (NodoSeleccion) corre justo después de dibujar el
// contenido: pinta el resaltado y lee de los píxeles de la pantalla el texto
// resaltado. Así lo que se ve resaltado es exactamente lo que se copia, en
// cualquier sección (listas, Chat, Modelos HF). La copia real se hace en el
// evento Custom que el render se manda a sí mismo.
#include "Tui/AplicacionTui.h"

#include "Tui/TemaTui.h"
#include "Util/Clipboard.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node_decorator.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/pixel.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace funshi {

namespace {

// Dibuja el hijo y después ejecuta un callback con la pantalla ya pintada.
class NodoSeleccion : public ftxui::NodeDecorator {
 public:
    NodoSeleccion(ftxui::Element hijo,
                  std::function<void(ftxui::Screen&)> alRenderizar)
        : ftxui::NodeDecorator(std::move(hijo)),
          alRenderizar_(std::move(alRenderizar)) {}

    void Render(ftxui::Screen& pantalla) override {
        ftxui::NodeDecorator::Render(pantalla);
        alRenderizar_(pantalla);
    }

 private:
    std::function<void(ftxui::Screen&)> alRenderizar_;
};

struct Punto {
    int x;
    int y;
};

}  // namespace

int AplicacionTui::ultimaFilaPanel(int altoPantalla) const {
    const SeccionTui seccion = seccionActual();
    const bool hayCaja =
        seccion != SeccionTui::Chat && seccion != SeccionTui::ModelosHF;
    // Sin la caja de comandos global faltan 2 filas (separador + caja).
    return altoPantalla -
           (hayCaja ? kFilasFueraDePaneles : kFilasFueraDePaneles - 2);
}

void AplicacionTui::limpiarSeleccion() {
    seleccionActiva_ = false;
    seleccionVisible_ = false;
    copiarAlSoltar_ = false;
    copiaPendiente_ = false;
    toastPendiente_ = false;
    textoSeleccionado_.clear();
    textoCopiado_.clear();
}

bool AplicacionTui::manejarRaton(ftxui::Event& evento) {
    const ftxui::Mouse& raton = evento.mouse();
    const SeccionTui seccion = seccionActual();
    const bool hayCaja =
        seccion != SeccionTui::Chat && seccion != SeccionTui::ModelosHF;

    const auto hayRango = [this]() {
        return seleccionInicioX_ != seleccionFinX_ ||
               seleccionInicioY_ != seleccionFinY_;
    };

    // 1. Arrastre en curso: sigue al cursor aunque salga del panel central.
    if (seleccionActiva_) {
        if (raton.motion == ftxui::Mouse::Moved) {
            seleccionFinX_ = raton.x;
            seleccionFinY_ = raton.y;
            seleccionVisible_ = hayRango();
            return true;
        }
        if (raton.motion == ftxui::Mouse::Released) {
            seleccionFinX_ = raton.x;
            seleccionFinY_ = raton.y;
            seleccionActiva_ = false;
            seleccionVisible_ = hayRango();
            // Copia final: el render la dispara cuando ya leyó el texto.
            copiarAlSoltar_ = seleccionVisible_;
            return true;
        }
    }

    // 2. Clic izquierdo: navegación o inicio de selección.
    if (raton.button == ftxui::Mouse::Left &&
        raton.motion == ftxui::Mouse::Pressed) {
        limpiarSeleccion();

        if (hayCaja && raton.y == filaCajaComandos()) {
            enfocarEntrada();
            return true;
        }
        if (raton.x < kColumnaPanelCentral) {
            const int fila = raton.y - kFilaPrimeraSeccion;
            if (fila < 0 || fila >= kCantidadSecciones) return false;
            irASeccion(fila);
            mensaje_ = std::string("Seccion: ") + nombreSeccion(seccionActual());
            mensajeEsError_ = false;
            return true;
        }

        // Fuera del área de paneles (barra superior o inferior): nada.
        if (raton.y < kFilaPaneles ||
            raton.y > ultimaFilaPanel(ftxui::Terminal::Size().dimy)) {
            return false;
        }

        seleccionActiva_ = true;
        seleccionInicioX_ = seleccionFinX_ = raton.x;
        seleccionInicioY_ = seleccionFinY_ = raton.y;

        // También mueve la selección de lista al ítem bajo el cursor.
        EstadoSeccion& estado = estadoActual();
        const int indice = estado.desplazamiento + (raton.y - kFilaPrimerItem);
        if (indice >= 0 && indice < static_cast<int>(estado.items.size())) {
            estado.seleccion = indice;
        }
        return true;
    }

    return false;
}

ftxui::Element AplicacionTui::conSeleccion(ftxui::Element contenido) {
    return std::make_shared<NodoSeleccion>(
        std::move(contenido),
        [this](ftxui::Screen& pantalla) { aplicarSeleccion(pantalla); });
}

// Se ejecuta dentro del render, con la pantalla del cuadro ya dibujada.
void AplicacionTui::aplicarSeleccion(ftxui::Screen& pantalla) {
    if (!seleccionVisible_) {
        textoSeleccionado_.clear();
        return;
    }

    // Región seleccionable: el panel central.
    const int xMin = kColumnaPanelCentral;
    const int xMax = pantalla.dimx() - 1;
    const int yMin = kFilaPaneles;
    const int yMax = std::min(pantalla.dimy() - 1, ultimaFilaPanel(pantalla.dimy()));
    if (xMax < xMin || yMax < yMin) return;

    const auto acotar = [&](int x, int y) {
        return Punto{std::clamp(x, xMin, xMax), std::clamp(y, yMin, yMax)};
    };
    Punto a = acotar(seleccionInicioX_, seleccionInicioY_);
    Punto b = acotar(seleccionFinX_, seleccionFinY_);
    if (b.y < a.y || (b.y == a.y && b.x < a.x)) std::swap(a, b);

    // Selección lineal, como en una terminal: la primera fila desde la
    // columna inicial, las del medio completas, la última hasta la final.
    std::string texto;
    for (int y = a.y; y <= b.y; ++y) {
        const int x0 = (y == a.y) ? a.x : xMin;
        const int x1 = (y == b.y) ? b.x : xMax;
        std::string linea;
        for (int x = x0; x <= x1; ++x) {
            ftxui::Pixel pixel = pantalla.PixelAt(x, y);
            linea += pixel.character;  // vacío en la 2.ª celda de un carácter ancho
            pixel.background_color = kAcento;
            pixel.foreground_color = kFondo;
        }
        while (!linea.empty() && linea.back() == ' ') linea.pop_back();
        if (y > a.y) texto += '\n';
        texto += linea;
    }
    while (!texto.empty() && texto.front() == '\n') texto.erase(0, 1);
    while (!texto.empty() && texto.back() == '\n') texto.pop_back();
    if (texto.find_first_not_of(" \n") == std::string::npos) texto.clear();

    textoSeleccionado_ = texto;
    if (texto.empty()) {
        copiarAlSoltar_ = false;
        return;
    }

    // Decide si hay que copiar. Durante el arrastre se copia cada vez que el
    // texto cambia, con un mínimo entre copias; al soltar siempre.
    const auto ahora = std::chrono::steady_clock::now();
    const bool cambio = texto != textoCopiado_;
    const bool toca = copiarAlSoltar_ ||
                      ahora - ultimaCopia_ >= std::chrono::milliseconds(kCopiaMinMs);
    const bool copiar = cambio && toca;
    const bool avisar = copiarAlSoltar_ || (copiar && textoCopiado_.empty());

    // Si el texto cambió pero aún no toca copiar y el usuario se queda quieto,
    // se pide un redibujado para copiar el texto final sin otro movimiento.
    if (cambio && !toca && !redibujoProgramado_) {
        redibujoProgramado_ = true;
        redibujarTras(kCopiaMinMs);
    }
    if (toca) redibujoProgramado_ = false;
    copiarAlSoltar_ = false;

    if (copiar || avisar) {
        copiaPendiente_ = copiaPendiente_ || copiar;
        toastPendiente_ = toastPendiente_ || avisar;
        // La copia (posible llamada al sistema) no se hace dentro del render.
        if (pantalla_) pantalla_->PostEvent(ftxui::Event::Custom);
    }
}

void AplicacionTui::procesarCopiaPendiente() {
    if (copiaPendiente_ && !textoSeleccionado_.empty()) {
        funshi::util::Clipboard::copy(textoSeleccionado_);
        textoCopiado_ = textoSeleccionado_;
        ultimaCopia_ = std::chrono::steady_clock::now();
    }
    copiaPendiente_ = false;
    if (toastPendiente_) {
        toastPendiente_ = false;
        mostrarToast("copied to the clipboard");
    }
}

void AplicacionTui::redibujarTras(int ms) {
    const auto compartido = compartido_;
    std::thread([compartido, ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        std::lock_guard<std::mutex> bloqueo(compartido->mutex);
        if (compartido->pantalla) {
            compartido->pantalla->PostEvent(ftxui::Event());
        }
    }).detach();
}

void AplicacionTui::mostrarToast(const std::string& mensaje) {
    toastMensaje_ = mensaje;
    toastVisible_ = true;
    toastInicio_ = std::chrono::steady_clock::now();
    if (pantalla_) {
        pantalla_->PostEvent(ftxui::Event());
    }
    // Redibuja al vencer el tiempo para que el toast desaparezca solo.
    redibujarTras(kToastDuracionMs + 50);
}

ftxui::Element AplicacionTui::renderToast() {
    if (!toastVisible_) return ftxui::emptyElement();

    const auto transcurrido =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - toastInicio_)
            .count();
    if (transcurrido >= kToastDuracionMs) {
        toastVisible_ = false;
        return ftxui::emptyElement();
    }

    // Caja del tamaño del mensaje; el texto conserva su color y el borde el suyo.
    ftxui::Element toast = ftxui::text(" " + toastMensaje_ + " ") | ftxui::bold |
                           ftxui::color(kExito) | ftxui::borderStyled(kAcento) |
                           ftxui::bgcolor(ftxui::Color::RGB(30, 30, 30));

    // Arriba a la derecha.
    return ftxui::vbox({
        ftxui::hbox({
            ftxui::filler(),
            std::move(toast),
        }),
        ftxui::filler(),
    });
}

}  // namespace funshi
