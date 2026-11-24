/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Nucleo/LineaComando.h"

#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace funshi {

std::vector<Token> tokenizar(const std::string& linea) {
    std::vector<Token> tokens;
    Token actual;
    char comilla = 0;
    // Profundidad de [] y {}: dentro de un valor JSON las comillas y los
    // espacios se conservan tal cual.
    int profundidad = 0;
    bool enCadenaJson = false;
    for (const char caracter : linea) {
        if (profundidad > 0) {
            actual.texto += caracter;
            if (enCadenaJson) {
                if (caracter == '"') enCadenaJson = false;
            } else if (caracter == '"') {
                enCadenaJson = true;
            } else if (caracter == '[' || caracter == '{') {
                ++profundidad;
            } else if (caracter == ']' || caracter == '}') {
                --profundidad;
            }
            continue;
        }
        if (comilla == 0 && (caracter == '[' || caracter == '{') &&
            actual.texto.find('=') != std::string::npos &&
            actual.texto.back() == '=') {
            profundidad = 1;
            actual.texto += caracter;
            continue;
        }
        if (comilla != 0) {
            if (caracter == comilla) {
                comilla = 0;
                continue;
            }
            actual.texto += caracter;
            continue;
        }
        if (caracter == '"' || caracter == '\'') {
            comilla = caracter;
            actual.entrecomillado = true;
            continue;
        }
        if (caracter == ' ' || caracter == '\t') {
            if (!actual.texto.empty()) {
                tokens.push_back(actual);
                actual = Token();
            }
            continue;
        }
        actual.texto += caracter;
    }
    if (!actual.texto.empty()) {
        tokens.push_back(actual);
    }
    return tokens;
}

bool esEntero(const std::string& texto) {
    if (texto.empty()) return false;
    std::size_t inicio = (texto[0] == '-' || texto[0] == '+') ? 1 : 0;
    if (inicio >= texto.size()) return false;
    for (std::size_t i = inicio; i < texto.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(texto[i]))) return false;
    }
    try {
        static_cast<void>(std::stoll(texto));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

namespace {

bool esDecimal(const std::string& texto) {
    if (texto.empty()) return false;
    std::size_t usados = 0;
    try {
        static_cast<void>(std::stod(texto, &usados));
    } catch (const std::exception&) {
        return false;
    }
    return usados == texto.size() &&
           texto.find_first_not_of("+-.0123456789eE") == std::string::npos;
}

}  // namespace

bool construirArgumentos(const std::vector<Token>& tokens, nlohmann::json& argumentos,
                         std::string& error) {
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& token = tokens[i].texto;
        const std::size_t separador = token.find('=');
        if (separador == std::string::npos) {
            error = "Argumento sin '=': " + token;
            return false;
        }
        const std::string clave = token.substr(0, separador);
        const std::string valor = token.substr(separador + 1);
        if (clave.empty()) {
            error = "Argumento sin nombre: " + token;
            return false;
        }
        if (tokens[i].entrecomillado) {
            argumentos[clave] = valor;
        } else if (esEntero(valor)) {
            // Los no negativos van como unsigned, igual que al parsear JSON:
            // varios comandos validan ids con number_unsigned.
            if (valor[0] == '-') {
                argumentos[clave] = std::stoll(valor);
            } else {
                argumentos[clave] = static_cast<std::uint64_t>(std::stoull(valor));
            }
        } else if (valor == "true" || valor == "false") {
            argumentos[clave] = valor == "true";
        } else if (!valor.empty() && (valor[0] == '[' || valor[0] == '{')) {
            try {
                argumentos[clave] = nlohmann::json::parse(valor);
            } catch (const std::exception&) {
                error = "JSON inválido en " + clave + ": " + valor;
                return false;
            }
        } else if (esDecimal(valor)) {
            argumentos[clave] = std::stod(valor);
        } else {
            argumentos[clave] = valor;
        }
    }
    return true;
}

}  // namespace funshi
