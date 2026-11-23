/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Sintaxis de línea de los comandos: `comando clave=valor clave="texto"`.
// La usan la caja de comandos de la TUI y la base de conocimiento del agente
// (para convertir los ejemplos de la ayuda en argumentos JSON).
#ifndef FUNSHI_NUCLEO_LINEA_COMANDO_H
#define FUNSHI_NUCLEO_LINEA_COMANDO_H

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace funshi {

// Pieza de la línea de comandos: el texto y si venía entrecomillado, para
// distinguir un valor numérico de uno textual.
struct Token {
    std::string texto;
    bool entrecomillado = false;
};

// Divide una línea en piezas, respetando comillas simples y dobles: el
// texto entrecomillado se une al token aunque tenga espacios.
std::vector<Token> tokenizar(const std::string& linea);

// Indica si un texto es un entero que cabe en un long long.
bool esEntero(const std::string& texto);

// Convierte los tokens "clave=valor" en argumentos de la solicitud. Sin
// comillas: enteros y decimales van como número, true/false como booleano y
// [..]/{..} como JSON. Entrecomillado, siempre como texto.
bool construirArgumentos(const std::vector<Token>& tokens, nlohmann::json& argumentos,
                         std::string& error);

}  // namespace funshi

#endif  // FUNSHI_NUCLEO_LINEA_COMANDO_H
