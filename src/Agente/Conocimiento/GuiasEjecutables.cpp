/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Conocimiento/GuiasEjecutables.h"

#include "Nucleo/AyudaComandos.h"

#include <algorithm>
#include <cctype>

namespace funshi::agente::conocimiento {

namespace {

// Fin del objeto {...} que empieza en `ini` (llaves balanceadas, respetando
// cadenas); npos si no cierra.
std::size_t finDeObjeto(const std::string& s, std::size_t ini) {
    int nivel = 0;
    bool enCadena = false;
    for (std::size_t i = ini; i < s.size(); ++i) {
        const char c = s[i];
        if (enCadena) {
            if (c == '\\') ++i;
            else if (c == '"') enCadena = false;
            continue;
        }
        if (c == '"') enCadena = true;
        else if (c == '{') ++nivel;
        else if (c == '}' && --nivel == 0) return i;
        else if (c == '\n') return std::string::npos;
    }
    return std::string::npos;
}

// Marcadores sin comillas tras ':' → P = "<proyecto>", MAYÚSCULAS = <minúsculas>.
std::string conMarcadores(const std::string& args) {
    std::string out;
    bool enCadena = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const char c = args[i];
        if (c == '"' && (i == 0 || args[i - 1] != '\\')) enCadena = !enCadena;
        if (!enCadena && c == ':' ) {
            out += c;
            std::size_t j = i + 1;
            while (j < args.size() && (std::isupper(static_cast<unsigned char>(args[j])) || args[j] == '_' ||
                                       std::isdigit(static_cast<unsigned char>(args[j])))) ++j;
            const std::string marca = args.substr(i + 1, j - i - 1);
            if (!marca.empty() && std::isupper(static_cast<unsigned char>(marca[0]))) {
                if (marca == "P") {
                    out += "\"<proyecto>\"";
                } else {
                    std::string bajo = marca;
                    std::transform(bajo.begin(), bajo.end(), bajo.begin(),
                                   [](unsigned char x) { return static_cast<char>(std::tolower(x)); });
                    out += "<" + bajo + ">";
                }
                i = j - 1;
            }
            continue;
        }
        out += c;
    }
    return out;
}

}  // namespace

std::string guiaEjecutable(const std::string& texto, const std::set<std::string>& herramientas,
                           std::vector<std::string>* comandos) {
    std::string out;
    std::size_t pos = 0;
    while (pos < texto.size()) {
        // Siguiente "comando.con.puntos {": nombre del catálogo seguido de un objeto.
        std::size_t llave = texto.find(" {", pos);
        if (llave == std::string::npos) break;
        std::size_t ini = llave;
        while (ini > pos && (std::isalnum(static_cast<unsigned char>(texto[ini - 1])) || texto[ini - 1] == '.')) --ini;
        const std::string nombre = texto.substr(ini, llave - ini);
        const std::size_t fin = finDeObjeto(texto, llave + 1);
        if (nombre.find('.') == std::string::npos || !buscarAyudaComando(nombre) || fin == std::string::npos) {
            out += texto.substr(pos, llave + 2 - pos);
            pos = llave + 2;
            continue;
        }
        const std::string args = conMarcadores(texto.substr(llave + 1, fin - llave));
        out += texto.substr(pos, ini - pos);
        if (herramientas.count(nombre)) {
            out += "{\"tool\":\"" + nombre + "\",\"arguments\":" + args + "}";
        } else {
            out += "{\"tool\":\"ejecutar_comando\",\"arguments\":{\"comando\":\"" + nombre + "\",\"argumentos\":" + args + "}}";
        }
        if (comandos && std::find(comandos->begin(), comandos->end(), nombre) == comandos->end()) {
            comandos->push_back(nombre);
        }
        pos = fin + 1;
    }
    out += texto.substr(std::min(pos, texto.size()));
    return out;
}

}  // namespace funshi::agente::conocimiento
