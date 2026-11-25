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
// Constantes visuales, dimensiones de layout y enum de secciones de la TUI.
#ifndef FUNSHI_TUI_TEMA_TUI_H
#define FUNSHI_TUI_TEMA_TUI_H

#include <cstddef>

#include <ftxui/screen/color.hpp>

namespace funshi {

// Paleta de la interfaz. No son constantes: la sección Configuración las
// cambia en vivo (Terminal/PreferenciasTui), solo desde el hilo de la UI, y
// se leen al dibujar cada cuadro. Los valores de aquí son los por defecto.
inline ftxui::Color kFondo = ftxui::Color::RGB(18, 18, 24);
inline ftxui::Color kFondoBarra = ftxui::Color::RGB(32, 34, 42);
inline ftxui::Color kFondoPanel = ftxui::Color::RGB(25, 26, 33);
inline ftxui::Color kFondoSeleccion = ftxui::Color::RGB(61, 65, 80);
inline ftxui::Color kFondoAviso = ftxui::Color::RGB(30, 30, 30);
inline ftxui::Color kTexto = ftxui::Color::RGB(216, 219, 228);
inline ftxui::Color kTextoTenue = ftxui::Color::RGB(122, 126, 140);
inline ftxui::Color kAcento = ftxui::Color::RGB(97, 175, 239);
inline ftxui::Color kExito = ftxui::Color::RGB(152, 195, 121);
inline ftxui::Color kAdvertencia = ftxui::Color::RGB(229, 192, 123);
inline ftxui::Color kError = ftxui::Color::RGB(224, 108, 117);

// Dominios de la TUI, en el orden del panel lateral.
enum class SeccionTui { Proyectos = 0, Escena, Scripts, Diagnosticos, Chat, ModelosHF, Proveedores, Configuracion };

// Cantidad de dominios del panel lateral.
constexpr int kCantidadSecciones = 8;

// Ancho del panel lateral, en columnas.
constexpr int kAnchoPanelLateral = 30;

// Ancho de la lista de comandos en el panel de ayuda, en columnas.
constexpr int kAnchoListaAyuda = 36;

// Columna en la que empieza el panel central (panel lateral + separador).
constexpr int kColumnaPanelCentral = kAnchoPanelLateral + 1;

// Filas que la pantalla reserva fuera del área de paneles: barra superior,
// separador, caja de comandos, separador, mensaje de estado y atajos.
constexpr int kFilasFueraDePaneles = 6;

// Filas que ocupa el encabezado de cada panel: su título y el separador que
// lo sigue.
constexpr int kFilasEncabezado = 2;

// Fila de la pantalla donde empieza el área de paneles, justo bajo la barra
// superior.
constexpr int kFilaPaneles = 1;

// Fila de la pantalla donde empieza la lista de ítems del panel central.
constexpr int kFilaPrimerItem = kFilaPaneles + kFilasEncabezado;

// Fila de la pantalla donde empieza la lista de dominios del panel lateral.
constexpr int kFilaPrimeraSeccion = kFilaPrimerItem;

// Filas que ocupa la barra inferior, contadas desde el final de la pantalla:
// caja de comandos, separador, mensaje y atajos.
constexpr int kFilasBarraInferior = 4;

// Nombre de cada sección para mostrar en la interfaz.
inline const char* nombreSeccion(SeccionTui seccion) {
    switch (seccion) {
        case SeccionTui::Proyectos: return "Proyectos";
        case SeccionTui::Escena: return "Escena";
        case SeccionTui::Scripts: return "Scripts";
        case SeccionTui::Diagnosticos: return "Diagnósticos";
        case SeccionTui::Chat: return "Chat Silvy";
        case SeccionTui::ModelosHF: return "Modelos HF";
        case SeccionTui::Proveedores: return "Proveedores";
        case SeccionTui::Configuracion: return "Configuración";
    }
    return "";
}

// Comando del catálogo que puebla cada sección.
inline const char* comandoDeSeccion(SeccionTui seccion) {
    switch (seccion) {
        case SeccionTui::Proyectos: return "project.list";
        case SeccionTui::Escena: return "scene.list";
        case SeccionTui::Scripts: return "script.list";
        case SeccionTui::Diagnosticos: return "diagnostics.list";
        case SeccionTui::Chat: return "";
        case SeccionTui::ModelosHF: return "";
        case SeccionTui::Proveedores: return "cli.config.get";
        case SeccionTui::Configuracion: return "cli.config.get";
    }
    return "";
}

// Campo del result donde cada sección encuentra su array de ítems.
inline const char* campoItemsDeSeccion(SeccionTui seccion) {
    switch (seccion) {
        case SeccionTui::Proyectos: return "projects";
        case SeccionTui::Escena: return "scenes";
        case SeccionTui::Scripts: return "scripts";
        case SeccionTui::Diagnosticos: return "items";
        case SeccionTui::Chat: return "";
        case SeccionTui::ModelosHF: return "";
        case SeccionTui::Proveedores: return "";
        case SeccionTui::Configuracion: return "";
    }
    return "";
}

inline std::size_t indiceSeccion(SeccionTui seccion) {
    return static_cast<std::size_t>(seccion);
}

}  // namespace funshi

#endif  // FUNSHI_TUI_TEMA_TUI_H
