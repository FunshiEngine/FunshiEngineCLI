/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Registro del proceso llama-server que lanza la CLI.
//
// llama-server se lanza desacoplado (nohup … &) para que sobreviva a la
// petición que lo creó, así que nadie lo detenía: quedaba vivo al salir de
// la TUI y cada cambio de modelo dejaba el anterior huérfano en otro puerto.
// Este registro guarda `<localInferenceDir>/llama-server.pid` con el PID del
// servidor y el del proceso dueño (la CLI que lo lanzó), y permite:
//   - detener el servidor anterior antes de arrancar otro;
//   - detenerlo al salir de la TUI (incluido por señal);
//   - limpiar al iniciar los servidores que dejó una sesión ya cerrada.

#include <filesystem>

namespace funshi::agente::llm {

// Anota `pid` como el servidor activo, propiedad de este proceso.
void registrarServidor(const std::filesystem::path& baseDir, int pid, int puerto);

// Detiene el servidor registrado (SIGTERM, y SIGKILL si no cae en ~3 s) y
// borra el registro. No hace nada si no hay registro o el proceso ya murió.
void detenerServidorRegistrado(const std::filesystem::path& baseDir);

// Al iniciar: si el dueño del registro ya no existe, detiene su servidor y,
// en Linux, también cualquier llama-server huérfano lanzado desde
// `directorioBinarios` (los binarios gestionados por la CLI). Si el dueño es
// otra CLI viva, no toca nada.
void limpiarServidoresHuerfanos(const std::filesystem::path& baseDir,
                                const std::filesystem::path& directorioBinarios);

// Instala manejadores de SIGTERM/SIGHUP/SIGINT/SIGQUIT que matan el servidor
// registrado por este proceso antes de terminar. Idempotente.
void instalarCierreServidorPorSenal();

}  // namespace funshi::agente::llm
