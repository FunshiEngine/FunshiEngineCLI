/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Lenguajes de programación y formatos de archivo con sus extensiones, y cómo
// los trata el motor. Lo usan la base de conocimiento (documento
// "guia.lenguajes") y el chat (cabecera del código que escribe el agente).
//
// La parte del motor no se copia: se pregunta a TiposArchivo::clasificar, así
// que refleja lo que reconoce la versión del motor enlazada.

#include <string>
#include <vector>

namespace funshi::agente::conocimiento {

struct Lenguaje {
    const char* nombre;
    std::vector<const char*> extensiones;  // en minúsculas, con punto
    const char* uso;
    bool esCodigo;  // lenguaje de programación (no datos ni documentos)
};

const std::vector<Lenguaje>& lenguajes();

// Lenguaje de una extensión (".java", ".CS" → C#); nullptr si no se conoce.
const Lenguaje* lenguajePorExtension(const std::string& extension);

// Extensión en minúsculas con punto de una ruta ("src/A.Java" → ".java").
std::string extensionDe(const std::string& ruta);

// Tipo que le da el motor a la ruta ("Script Java", "Prefab"…); vacío si el
// motor no la reconoce.
std::string tipoEnMotor(const std::string& ruta);

// ¿El motor ejecuta como script un archivo con esta ruta?
bool esScriptDelMotor(const std::string& ruta);

// Extensión que el motor reconoce como archivo de proyecto (vacía si no
// reconoce ninguna).
std::string extensionDeProyecto();

// Texto del documento de conocimiento: lenguajes y extensiones, archivos del
// motor y scripts que ejecuta.
std::string documentoLenguajes();

}  // namespace funshi::agente::conocimiento
