/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Separa el razonamiento que algunos modelos escriben dentro del texto, entre
// <think> y </think> (Qwen3, DeepSeek-R1 sin --reasoning-format), del texto
// de la respuesta. Las etiquetas pueden llegar partidas entre fragmentos del
// stream: lo que podría ser el comienzo de una etiqueta se retiene hasta el
// fragmento siguiente.

#include <string>

namespace funshi::agente::llm {

class SeparadorPensamiento {
public:
    struct Partes {
        std::string texto;
        std::string pensamiento;
    };

    // Procesa un fragmento y devuelve lo que ya se puede emitir.
    Partes agregar(const std::string& fragmento);
    // Fin del stream: entrega lo retenido.
    Partes terminar();

private:
    bool dentro_ = false;
    std::string pendiente_;
};

}  // namespace funshi::agente::llm
