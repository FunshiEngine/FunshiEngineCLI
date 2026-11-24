/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Instrucciones de sistema de Silvy y de sus subagentes. Concentran las
// reglas de trabajo: cómo encontrar comandos (RAG), cuándo delegar en
// subagentes y cómo debe escribirse cada tarea delegada.

#include <string>

namespace funshi::agente {

struct TareaSubagente {
    std::string titulo;
    std::string instrucciones;
    std::string contexto;
    std::string perfil = "completo";
};

// Prompt del agente principal. `conSubagentes`: incluir las reglas de
// delegación (solo si la herramienta lanzar_subagentes está disponible).
// `proyectoActivo`: proyecto que el usuario tiene seleccionado (puede ir vacío).
std::string instruccionesPrincipal(bool conSubagentes, const std::string& proyectoActivo);

// Prompt de un subagente para una tarea concreta.
// `proyectoActivo`: el de la sesión del usuario (vacío si no hay).
std::string instruccionesSubagente(const TareaSubagente& tarea, const std::string& proyectoActivo = "");

}  // namespace funshi::agente
