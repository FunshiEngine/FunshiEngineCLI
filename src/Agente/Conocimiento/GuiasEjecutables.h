/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Las guías se escriben con pasos "comando {argumentos}" y marcadores
// (P = proyecto, ID, PADRE, RUTA…). Para el modelo, cada paso se convierte en
// la llamada que tiene que hacer: la herramienta propia del comando o
// ejecutar_comando, con el proyecto entre comillas y los demás marcadores
// como <id>, <padre>… para que se vea qué hay que reemplazar.

#include <set>
#include <string>
#include <vector>

namespace funshi::agente::conocimiento {

// Texto de la guía con cada paso convertido. `comandos` recibe los comandos
// que nombra, en orden y sin repetir.
std::string guiaEjecutable(const std::string& texto, const std::set<std::string>& herramientas,
                           std::vector<std::string>* comandos = nullptr);

}  // namespace funshi::agente::conocimiento
