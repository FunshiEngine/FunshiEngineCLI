/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Sesiones de chat con Silvy: cada una es una conversación independiente,
// con su historial del modelo y los mensajes que ve el usuario, guardada en
// `<carpeta de config>/sesiones/<id>.json` para retomarla después.

#include "Agente/Silvy.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace funshi::agente {

struct MensajeSesion {
    // "usuario" | "silvy" | "pensamiento" | "herramienta" | "subagente" | "estado" | "error"
    std::string rol;
    std::string texto;
    std::string detalle;  // texto completo (herramientas: argumentos y resultado)
    std::string ref;      // "subagente": id del subagente
};

// Trabajo de un subagente: la tarea que recibió y lo que hizo, para verlo
// aparte desde el chat.
struct SubagenteSesion {
    std::string id;
    std::string titulo;
    std::string perfil;
    std::string instrucciones;
    std::string contexto;
    std::string estado;  // "en curso" | "completado" | "fallido"
    std::vector<MensajeSesion> mensajes;
};

struct SesionChat {
    std::string id;
    std::string titulo;
    std::int64_t creada = 0;       // segundos desde epoch
    std::int64_t actualizada = 0;
    std::vector<MensajeSesion> mensajes;
    std::vector<SubagenteSesion> subagentes;  // del más viejo al más nuevo
    SesionAgente agente;           // historial del modelo + proyecto activo

    nlohmann::json aJson() const;
    static SesionChat desdeJson(const nlohmann::json& j);

    // Sesión vacía con id único y título "Sesión N" si no se indica.
    static SesionChat nueva(const std::string& titulo = "");
    // Título a partir del primer pedido (primeras palabras, ~40 caracteres).
    static std::string tituloDesde(const std::string& pedido);
};

class AlmacenSesiones {
public:
    explicit AlmacenSesiones(std::filesystem::path dir = dirPorDefecto());
    static std::filesystem::path dirPorDefecto();

    // Todas las sesiones guardadas, la más reciente primero. Ignora archivos dañados.
    std::vector<SesionChat> cargarTodas() const;
    // Escritura atómica (.tmp + rename). Recorta el historial del modelo
    // a los últimos kMaxHistorial mensajes (conserva el de sistema).
    bool guardar(const SesionChat& sesion) const;
    bool borrar(const std::string& id) const;

    const std::filesystem::path& dir() const { return dir_; }
    static constexpr std::size_t kMaxHistorial = 200;
    static constexpr std::size_t kMaxMensajes = 500;
    static constexpr std::size_t kMaxSubagentes = 40;  // los más recientes

private:
    std::filesystem::path dir_;
};

}  // namespace funshi::agente
