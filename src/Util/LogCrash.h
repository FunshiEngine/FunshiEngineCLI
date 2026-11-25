/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

#include <string>

namespace funshi::util {

// Instala manejadores de crash (SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS) y
// std::terminate para escribir un log con backtrace antes de terminar.
// Seguro llamar varias veces; solo instala una vez.
void instalarManejadorCrash(const std::string& rutaLog = {});

// Ruta del último log de crash escrito en esta ejecución (vacía si ninguno).
std::string rutaUltimoLogCrash();

// Escribe un mensaje en el log de crash (best-effort, no async-signal-safe).
void anotarCrash(const std::string& mensaje);

}  // namespace funshi::util
