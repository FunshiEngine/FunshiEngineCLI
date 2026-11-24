/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Modelo de datos del chat de la TUI: mensajes (con su caché de dibujo),
// subagentes con su propia transcripción y sesiones. Lo llenan el monitor
// (MonitorChat) y las órdenes de sesión; lo dibuja AplicacionTui_ChatPanel.

#include "Agente/Sesiones/SesionChat.h"

#include <ftxui/screen/color.hpp>

#include <memory>
#include <string>
#include <vector>

namespace funshi::chat {

// Línea ya partida al ancho del panel, lista para dibujar.
struct LineaChat {
    std::string texto;
    ftxui::Color color;
    bool bold = false;
    bool tenue = false;
};

struct MensajeChat {
    // Pensamiento: razonamiento del modelo o lo que escribió antes de usar
    //              una herramienta.
    // Herramienta: una llamada con sus argumentos y su resultado.
    // Subagente:   resumen vivo de un subagente (`ref` = su id).
    // Tarea:       la tarea que recibió un subagente (en su vista).
    // Codigo:      contenido que una herramienta escribe en un archivo.
    enum class Rol { Usuario, Silvy, Pensamiento, Herramienta, Subagente, Tarea, Codigo, Estado, Error };
    Rol rol = Rol::Estado;
    std::string texto;
    std::string detalle;    // se muestra completo al expandir (clic)
    std::string ref;        // Subagente: id del subagente
    bool enCurso = false;
    bool ok = true;         // Herramienta/Subagente: terminó bien
    bool expandido = false;
    // Cada cambio incrementa `version`; las líneas partidas se recalculan
    // solo si cambió la versión o el ancho (no en cada frame).
    unsigned version = 0;
    unsigned versionCache = ~0u;
    int anchoCache = -1;
    std::vector<LineaChat> lineasCache;

    void tocar() { ++version; }
};

std::string rolATexto(MensajeChat::Rol r);
MensajeChat::Rol rolDesdeTexto(const std::string& t);
MensajeChat mensajeDesdeSesion(const agente::MensajeSesion& m);
agente::MensajeSesion mensajeASesion(const MensajeChat& m);

// Un subagente lanzado desde la sesión: su tarea y lo que va haciendo.
struct SubagenteTui {
    agente::SubagenteSesion datos;  // `datos.mensajes` solo se usa al guardar
    std::vector<MensajeChat> mensajes;
    int numero = 0;                 // 1, 2, … dentro de la sesión (/sub N)
    int indice = 0, total = 0;      // posición en su tanda (1/2, 2/2)
    int acciones = 0;
    std::string ultimaAccion;
    bool enCurso = false;
    int desplazamiento = 0;     // líneas desde el final (0 = siguiendo lo nuevo)
    int lineasDibujadas = -1;   // total del último frame, para anclar la vista
};

// Sesiones de chat. Cada una tiene su conversación con Silvy (persistida en
// <config>/sesiones/) y sus mensajes con caché de dibujo. Varias pueden estar
// procesando a la vez: el hilo de cada orden guarda un shared_ptr a la suya.
struct SesionTui {
    agente::SesionChat datos;
    std::vector<MensajeChat> mensajes;
    std::vector<std::shared_ptr<SubagenteTui>> subagentes;
    std::string vista;  // id del subagente a la vista; vacío = la conversación
    bool ocupado = false;
    int desplazamiento = 0;     // líneas desde el final (0 = siguiendo lo nuevo)
    int lineasDibujadas = -1;   // total del último frame, para anclar la vista

    std::shared_ptr<SubagenteTui> subagente(const std::string& id) const;
    // Pasa de/hacia el formato persistido (mensajes y subagentes).
    void cargarDesdeDatos();
    void volcarEnDatos();
};

}  // namespace funshi::chat
