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

// Paleta del tema oscuro de la interfaz.
inline const ftxui::Color kFondo = ftxui::Color::RGB(18, 18, 24);
inline const ftxui::Color kFondoBarra = ftxui::Color::RGB(32, 34, 42);
inline const ftxui::Color kFondoPanel = ftxui::Color::RGB(25, 26, 33);
inline const ftxui::Color kFondoSeleccion = ftxui::Color::RGB(61, 65, 80);
inline const ftxui::Color kTexto = ftxui::Color::RGB(216, 219, 228);
inline const ftxui::Color kTextoTenue = ftxui::Color::RGB(122, 126, 140);
inline const ftxui::Color kAcento = ftxui::Color::RGB(97, 175, 239);
inline const ftxui::Color kExito = ftxui::Color::RGB(152, 195, 121);
inline const ftxui::Color kAdvertencia = ftxui::Color::RGB(229, 192, 123);
inline const ftxui::Color kError = ftxui::Color::RGB(224, 108, 117);

// Dominios de la TUI, en el orden del panel lateral.
enum class SeccionTui { Proyectos = 0, Escena, Scripts, Diagnosticos, Chat, ModelosHF };

// Cantidad de dominios del panel lateral.
constexpr int kCantidadSecciones = 6;

// Ancho del panel lateral, en columnas.
constexpr int kAnchoPanelLateral = 30;

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
    }
    return "";
}

inline std::size_t indiceSeccion(SeccionTui seccion) {
    return static_cast<std::size_t>(seccion);
}

}  // namespace funshi

#endif  // FUNSHI_TUI_TEMA_TUI_H
