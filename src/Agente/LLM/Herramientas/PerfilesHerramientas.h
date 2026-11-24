/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Perfiles de herramientas: qué comandos puede usar un agente.
//
//   lectura   solo consultas (listar, obtener, buscar)
//   escena    lectura + modificar la escena (objetos, componentes, jerarquía)
//   archivos  lectura + archivos de src/, vfs fs/ e importación de assets
//   scripts   lectura + scripts (crear en src/, compilar, recargar, campos)
//   completo  todo (el agente principal y los subagentes que lo necesiten)
//
// El filtro se aplica tanto a las herramientas propias como a
// ejecutar_comando, así un subagente de lectura no puede modificar nada.

#include "Agente/Harness.h"
#include "Agente/LLM/Bucle/ToolRegistry.h"

#include <memory>
#include <string>
#include <vector>

namespace funshi::agente::llm {

struct PerfilHerramientas {
    const char* nombre;
    const char* descripcion;
};

const std::vector<PerfilHerramientas>& perfilesHerramientas();

bool perfilValido(const std::string& perfil);

// El comando `comando` está permitido en `perfil`.
bool comandoPermitido(const std::string& perfil, const std::string& comando);

// Registro con las herramientas del motor permitidas en `perfil`, más
// buscar_comandos y ejecutar_comando (filtrado por el mismo perfil).
std::shared_ptr<ToolRegistry> crearRegistroPerfil(std::shared_ptr<Harness> harness,
                                                  const std::string& perfil);

}  // namespace funshi::agente::llm
