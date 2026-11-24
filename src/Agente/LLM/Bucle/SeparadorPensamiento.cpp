/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Bucle/SeparadorPensamiento.h"

#include <algorithm>

namespace funshi::agente::llm {

namespace {

constexpr const char* kAbre = "<think>";
constexpr const char* kCierra = "</think>";

// Largo del sufijo de `s` que es un prefijo propio de `etiqueta` (una
// etiqueta que podría completarse en el próximo fragmento).
std::size_t prefijoAlFinal(const std::string& s, const std::string& etiqueta) {
    const std::size_t max = std::min(s.size(), etiqueta.size() - 1);
    for (std::size_t n = max; n > 0; --n) {
        if (s.compare(s.size() - n, n, etiqueta, 0, n) == 0) return n;
    }
    return 0;
}

}  // namespace

SeparadorPensamiento::Partes SeparadorPensamiento::agregar(const std::string& fragmento) {
    Partes p;
    std::string buf = pendiente_ + fragmento;
    pendiente_.clear();
    while (!buf.empty()) {
        const std::string etiqueta = dentro_ ? kCierra : kAbre;
        std::string& destino = dentro_ ? p.pensamiento : p.texto;
        const auto pos = buf.find(etiqueta);
        if (pos != std::string::npos) {
            destino += buf.substr(0, pos);
            buf.erase(0, pos + etiqueta.size());
            dentro_ = !dentro_;
            continue;
        }
        const std::size_t retener = prefijoAlFinal(buf, etiqueta);
        destino += buf.substr(0, buf.size() - retener);
        pendiente_ = buf.substr(buf.size() - retener);
        break;
    }
    return p;
}

SeparadorPensamiento::Partes SeparadorPensamiento::terminar() {
    Partes p;
    (dentro_ ? p.pensamiento : p.texto) = pendiente_;
    pendiente_.clear();
    dentro_ = false;
    return p;
}

}  // namespace funshi::agente::llm
