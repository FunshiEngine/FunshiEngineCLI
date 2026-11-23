/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Documentación de uso de cada comando del catálogo para la ayuda de la TUI.
#ifndef FUNSHI_TUI_AYUDA_COMANDOS_H
#define FUNSHI_TUI_AYUDA_COMANDOS_H

#include <string>
#include <vector>

namespace funshi {

struct ArgumentoAyuda {
    std::string nombre;
    std::string tipo;
    bool requerido = false;
    std::string descripcion;
};

struct AyudaComando {
    std::string comando;
    std::string descripcion;
    // El comando necesita args.project; la TUI lo completa con el proyecto
    // activo si no se escribe.
    bool usaProyecto = false;
    std::vector<ArgumentoAyuda> argumentos;
    // Ejemplo listo para la caja de comandos (sin project= si usaProyecto).
    std::string ejemplo;
};

// Ayuda de todos los comandos, en el orden del catálogo.
const std::vector<AyudaComando>& ayudaComandos();

// Ayuda de un comando por su texto; nullptr si no existe.
const AyudaComando* buscarAyudaComando(const std::string& comando);

}  // namespace funshi

#endif  // FUNSHI_TUI_AYUDA_COMANDOS_H
