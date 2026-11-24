/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#ifndef FUNSHI_COMANDOS_ESCENA_INTERNO_H
#define FUNSHI_COMANDOS_ESCENA_INTERNO_H

// Utilidades compartidas entre los ComandosEscena*.cpp (definidas en
// ComandosEscenaComun.cpp). No es API pública: no incluir fuera de Comandos/.

#include <nlohmann/json.hpp>

#include <string>

#include "Escena/ContextoEscena.h"
#include "Comandos/ComandosComun.h"
#include "Nucleo/Respuesta.h"

class Component;

namespace funshi {

// Nombre legible del tipo de un componente ("Transform", "Script", ...).
std::string tipoComponenteNombre(Component* comp);

// Historial de undo/redo en <proyecto>/Memory/undo_history.json.
std::string rutaHistorialUndo(const std::string& nombreProyecto);
bool cargarHistorial(const std::string& ruta, nlohmann::json& historial);
bool guardarHistorial(const std::string& ruta, const nlohmann::json& historial);
void registrarOperacionUndo(const std::string& nombreProyecto,
                            const std::string& tipo,
                            int idObjeto,
                            const nlohmann::json& antes,
                            const nlohmann::json& despues);

// Guarda la escena con la convención de rutas del motor.
void guardarEscena(ContextoEscena& contexto, const std::string& nombreProyecto);

}  // namespace funshi

#endif  // FUNSHI_COMANDOS_ESCENA_INTERNO_H
