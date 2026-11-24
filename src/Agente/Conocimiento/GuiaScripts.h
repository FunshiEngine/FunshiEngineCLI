/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Scripts del motor: cómo tiene que ser un .java o un .cpp para que el motor
// lo cargue como comportamiento de un objeto. La plantilla sale del propio
// motor (FileManager::plantillaScript), así la guía sigue sus cambios.

#include <string>

namespace funshi::agente::conocimiento {

// Guía para la base de conocimiento: reglas, API, plantillas y ejemplos.
std::string documentoScripts();

// Plantilla del motor para la clase del archivo (.java o .cpp); vacío si la
// ruta no es un script del motor.
std::string plantillaDeScript(const std::string& ruta);

// Qué tiene mal un script para el motor (vacío si nada): clase con otro
// nombre que el archivo, `package`, `main`, estilo de otro motor, o un .cpp
// sin IScriptBehaviour o sin la fábrica FUNSHI_CREAR_COMPORTAMIENTO.
std::string problemaDeScript(const std::string& ruta, const std::string& contenido);

}  // namespace funshi::agente::conocimiento
