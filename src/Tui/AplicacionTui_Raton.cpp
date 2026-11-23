/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// AplicacionTui — ratón: selección resaltada y copia al portapapeles.
//
// Funcionamiento: el ratón solo guarda coordenadas de pantalla. Un nodo
// envoltorio (NodoSeleccion) corre justo después de dibujar el contenido:
// pinta el resaltado y lee de los píxeles de la pantalla el texto resaltado.
// Así lo que se ve resaltado es exactamente lo que se copia, en cualquier
// sección (listas, Chat, Modelos). La copia no se hace dentro del render: el
// render se manda un evento Custom y la copia corre en segundo plano
// (AplicacionTui_Portapapeles.cpp).
//
//   arrastrar      selección libre (como en una terminal)
//   doble clic     palabra
//   triple clic    línea
#include "Tui/AplicacionTui.h"

#include "Tui/Terminal/TemaTui.h"
#include "Tui/Vistas/AyudaComandos.h"
#include "Util/Clipboard.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node_decorator.hpp>
#include <ftxui/screen/pixel.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
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
    NodoSeleccion(ftxui::Element hijo, std::function<void(ftxui::Screen&)> alRenderizar)
        : ftxui::NodeDecorator(std::move(hijo)), alRenderizar_(std::move(alRenderizar)) {}

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

// Separadores de palabra: espacio y trazos de cajas (U+2500–U+257F), que
// son los bordes de los paneles. La 2.ª celda de un carácter ancho llega
// vacía y cuenta como parte de la palabra.
bool esSeparador(const std::string& c) {
    if (c == " ") return true;
    if (c.empty()) return false;  // 2.ª celda de un carácter ancho
    return c.size() == 3 && static_cast<unsigned char>(c[0]) == 0xE2 &&
           (static_cast<unsigned char>(c[1]) == 0x94 || static_cast<unsigned char>(c[1]) == 0x95);
}

}  // namespace

int AplicacionTui::ultimaFilaPanel(int altoPantalla) const {
    const SeccionTui seccion = seccionActual();
    const bool hayCaja = seccion != SeccionTui::Chat && seccion != SeccionTui::ModelosHF;
    // Sin la caja de comandos global faltan 2 filas (separador + caja).
    return altoPantalla - (hayCaja ? kFilasFueraDePaneles : kFilasFueraDePaneles - 2);
}

void AplicacionTui::limpiarSeleccion() {
    seleccionActiva_ = false;
    seleccionVisible_ = false;
    copiarAlSoltar_ = false;
    copiaPendiente_ = false;
    modoSeleccion_ = ModoSeleccion::Libre;
    textoSeleccionado_.clear();
    textoCopiado_.clear();
}

bool AplicacionTui::manejarRaton(ftxui::Event& evento) {
    const ftxui::Mouse& raton = evento.mouse();
    const SeccionTui seccion = seccionActual();
    const bool hayCaja = seccion != SeccionTui::Chat && seccion != SeccionTui::ModelosHF;
    const auto ahora = std::chrono::steady_clock::now();

    // 1. Botón presionado: arrastre o fin de la selección.
    if (seleccionActiva_) {
        if (raton.motion == ftxui::Mouse::Moved) {
            if (raton.x == seleccionFinX_ && raton.y == seleccionFinY_) return true;
            seleccionFinX_ = raton.x;
            seleccionFinY_ = raton.y;
            // Arrastrar tras un doble/triple clic pasa a selección libre.
            modoSeleccion_ = ModoSeleccion::Libre;
            seleccionVisible_ =
                seleccionInicioX_ != seleccionFinX_ || seleccionInicioY_ != seleccionFinY_;
            ultimoMovimiento_ = ahora;
            return true;
        }
        if (raton.motion == ftxui::Mouse::Released) {
            seleccionActiva_ = false;
            if (modoSeleccion_ == ModoSeleccion::Libre) {
                seleccionFinX_ = raton.x;
                seleccionFinY_ = raton.y;
                seleccionVisible_ =
                    seleccionInicioX_ != seleccionFinX_ || seleccionInicioY_ != seleccionFinY_;
            }
            copiarAlSoltar_ = seleccionVisible_;
            return true;
        }
    }

    // 2. Clic izquierdo: navegación o inicio de selección.
    if (raton.button == ftxui::Mouse::Left && raton.motion == ftxui::Mouse::Pressed) {
        // Doble/triple clic: misma celda y poco tiempo desde el anterior.
        const bool seguido = raton.x == ultimoClicX_ && raton.y == ultimoClicY_ &&
                             ahora - ultimoClic_ <= std::chrono::milliseconds(kDobleClicMs);
        clicsSeguidos_ = seguido ? std::min(clicsSeguidos_ + 1, 3) : 1;
        ultimoClic_ = ahora;
        ultimoClicX_ = raton.x;
        ultimoClicY_ = raton.y;
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
        if (raton.y < kFilaPaneles || raton.y > ultimaFilaPanel(ftxui::Terminal::Size().dimy)) {
            return false;
        }

        seleccionActiva_ = true;
        seleccionInicioX_ = seleccionFinX_ = raton.x;
        seleccionInicioY_ = seleccionFinY_ = raton.y;
        ultimoMovimiento_ = ahora;
        modoSeleccion_ = clicsSeguidos_ == 1   ? ModoSeleccion::Libre
                         : clicsSeguidos_ == 2 ? ModoSeleccion::Palabra
                                               : ModoSeleccion::Linea;
        seleccionVisible_ = modoSeleccion_ != ModoSeleccion::Libre;
        if (seleccionVisible_) return true;  // doble/triple clic: solo selecciona

        // Con la ayuda abierta, el clic elige el comando bajo el cursor.
        if (ayudaVisible_) {
            const int indice = ayudaDesplazamiento_ + (raton.y - kFilaPrimerItem);
            if (raton.x < kColumnaPanelCentral + kAnchoListaAyuda && indice >= 0 &&
                indice < static_cast<int>(ayudaComandos().size())) {
                ayudaSeleccion_ = indice;
            }
            return true;
        }
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
        std::move(contenido), [this](ftxui::Screen& pantalla) { aplicarSeleccion(pantalla); });
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

    // Texto de una celda. FTXUI deja vacías tanto la 2.ª celda de un carácter
    // ancho (no aporta texto) como las celdas que nadie dibujó (son espacio).
    const auto celda = [&](int x, int y) -> std::string {
        const std::string& c = pantalla.PixelAt(x, y).character;
        if (!c.empty()) return c;
        if (x > 0 && ftxui::string_width(pantalla.PixelAt(x - 1, y).character) == 2) return {};
        return " ";
    };
    Punto a{std::clamp(seleccionInicioX_, xMin, xMax), std::clamp(seleccionInicioY_, yMin, yMax)};
    Punto b{std::clamp(seleccionFinX_, xMin, xMax), std::clamp(seleccionFinY_, yMin, yMax)};

    if (modoSeleccion_ == ModoSeleccion::Palabra) {
        if (esSeparador(celda(a.x, a.y))) {  // doble clic en un espacio: nada
            seleccionVisible_ = false;
            textoSeleccionado_.clear();
            return;
        }
        b = a;
        while (a.x > xMin && !esSeparador(celda(a.x - 1, a.y))) --a.x;
        while (b.x < xMax && !esSeparador(celda(b.x + 1, b.y))) ++b.x;
    } else if (modoSeleccion_ == ModoSeleccion::Linea) {
        // Del primer al último carácter visible de la fila (sin bordes).
        b = a;
        a.x = xMin;
        b.x = xMax;
        while (a.x < b.x && esSeparador(celda(a.x, a.y))) ++a.x;
        while (b.x > a.x && esSeparador(celda(b.x, b.y))) --b.x;
    } else if (b.y < a.y || (b.y == a.y && b.x < a.x)) {
        std::swap(a, b);
    }

    // Selección lineal, como en una terminal: la primera fila desde la
    // columna inicial, las del medio completas, la última hasta la final.
    std::string texto;
    for (int y = a.y; y <= b.y; ++y) {
        const int x0 = (y == a.y) ? a.x : xMin;
        const int x1 = (y == b.y) ? b.x : xMax;
        std::string linea;
        for (int x = x0; x <= x1; ++x) {
            linea += celda(x, y);
            ftxui::Pixel& pixel = pantalla.PixelAt(x, y);  // referencia: el resaltado se pinta
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

    // ¿Copiar? Al soltar el botón (o tras doble/triple clic), o como respaldo
    // si el botón sigue "presionado" pero el ratón lleva un rato quieto.
    const bool quieto = seleccionActiva_ && modoSeleccion_ == ModoSeleccion::Libre &&
                        std::chrono::steady_clock::now() - ultimoMovimiento_ >=
                            std::chrono::milliseconds(kCopiaInactividadMs);
    // Respaldo para terminales que no informan el soltar del botón: un único
    // temporizador vuelve a dibujar cuando se cumpla la pausa.
    if (seleccionActiva_ && modoSeleccion_ == ModoSeleccion::Libre && !quieto &&
        !compartido_->temporizadorCopia.exchange(true)) {
        const auto falta = std::chrono::milliseconds(kCopiaInactividadMs) -
                           (std::chrono::steady_clock::now() - ultimoMovimiento_);
        const auto compartido = compartido_;
        const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(falta).count()) + 20;
        std::thread([compartido, ms]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(ms, 1)));
            compartido->temporizadorCopia = false;
            std::lock_guard<std::mutex> bloqueo(compartido->mutex);
            if (compartido->pantalla) compartido->pantalla->PostEvent(ftxui::Event());
        }).detach();
    }
    const bool copiar = !texto.empty() && (copiarAlSoltar_ || (quieto && texto != textoCopiado_));
    copiarAlSoltar_ = false;
    if (copiar && !copiaPendiente_) {
        copiaPendiente_ = true;
        if (pantalla_) pantalla_->PostEvent(ftxui::Event::Custom);
    }
}

void AplicacionTui::redibujarTras(int ms) {
    const auto compartido = compartido_;
    std::thread([compartido, ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        std::lock_guard<std::mutex> bloqueo(compartido->mutex);
        if (compartido->pantalla) compartido->pantalla->PostEvent(ftxui::Event());
    }).detach();
}

}  // namespace funshi
