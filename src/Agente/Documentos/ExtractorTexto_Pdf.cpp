/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Texto de PDF: pdftotext (poppler) si está instalado; si no, un extractor
// propio para PDFs comunes: infla los streams FlateDecode (miniz) y toma el
// texto de los operadores Tj, TJ, ' y ". No resuelve fuentes con CMap (texto
// en <hex> de fuentes CID), cifrado ni PDFs escaneados.
#include "Agente/Documentos/ExtractorTexto.h"

#include <miniz/miniz.h>

#include <array>
#include <cstdio>
#include <string>

namespace funshi::agente::documentos {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
#define FUNSHI_POPEN _popen
#define FUNSHI_PCLOSE _pclose
#else
#define FUNSHI_POPEN popen
#define FUNSHI_PCLOSE pclose
#endif

// Ruta entre comillas para el intérprete de comandos del sistema.
std::string entreComillas(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
#endif
}

// Cadena literal (...) de PDF desde `i` (apunta al '('); deja `i` tras ')'.
std::string leerLiteral(const std::string& s, std::size_t& i) {
    std::string out;
    int nivel = 0;
    for (++i; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            const char e = s[++i];
            switch (e) {
                case 'n': out += '\n'; break;
                case 'r': break;
                case 't': out += ' '; break;
                case 'b': case 'f': break;
                case '(': case ')': case '\\': out += e; break;
                default:
                    if (e >= '0' && e <= '7') {
                        int v = e - '0';
                        for (int k = 0; k < 2 && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '7'; ++k) v = v * 8 + (s[++i] - '0');
                        out += static_cast<char>(v);
                    }
            }
        } else if (c == '(') {
            ++nivel;
            out += c;
        } else if (c == ')') {
            if (nivel-- == 0) { ++i; break; }
            out += c;
        } else {
            out += c;
        }
    }
    return out;
}

// Texto de un stream de contenido ya descomprimido.
std::string textoDeContenido(const std::string& s) {
    std::string out, pendiente;
    bool enArreglo = false;
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '(') {
            pendiente += leerLiteral(s, i);
            continue;
        }
        if (c == '[') { enArreglo = true; ++i; continue; }
        if (c == ']') { enArreglo = false; ++i; continue; }
        if (enArreglo && (c == '-' || (c >= '0' && c <= '9'))) {
            // Desplazamiento grande dentro de TJ ≈ espacio entre palabras.
            std::size_t j = i;
            while (j < s.size() && (s[j] == '-' || s[j] == '.' || (s[j] >= '0' && s[j] <= '9'))) ++j;
            if (std::atof(s.substr(i, j - i).c_str()) < -200 && !pendiente.empty() && pendiente.back() != ' ') pendiente += ' ';
            i = j;
            continue;
        }
        if (c == 'T' && i + 1 < s.size()) {
            const char o = s[i + 1];
            if (o == 'j' || o == 'J') { out += pendiente; pendiente.clear(); }
            else if (o == 'd' || o == 'D' || o == '*') { out += pendiente; pendiente.clear(); if (!out.empty() && out.back() != '\n') out += '\n'; }
            i += 2;
            continue;
        }
        if ((c == '\'' || c == '"') && !pendiente.empty()) { out += pendiente + "\n"; pendiente.clear(); }
        if (c == 'E' && i + 1 < s.size() && s[i + 1] == 'T') { out += pendiente; pendiente.clear(); out += '\n'; i += 2; continue; }
        ++i;
    }
    return out + pendiente;
}

}  // namespace

std::string textoDePdfConHerramienta(const fs::path& archivo, std::string& metodo) {
    const std::string comando = "pdftotext -layout -enc UTF-8 -q " + entreComillas(archivo.string()) + " -";
    FILE* tubo = FUNSHI_POPEN(comando.c_str(), "r");
    if (!tubo) return {};
    std::string out;
    std::array<char, 8192> buf{};
    std::size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), tubo)) > 0) out.append(buf.data(), n);
    const int estado = FUNSHI_PCLOSE(tubo);
    if (estado != 0) return {};
    metodo = "pdftotext";
    return out;
}

std::string textoDePdf(const std::string& bytes) {
    std::string out;
    std::size_t pos = 0;
    while ((pos = bytes.find("stream", pos)) != std::string::npos) {
        // "endstream" también contiene "stream": se salta.
        if (pos >= 3 && bytes.compare(pos - 3, 3, "end") == 0) { pos += 6; continue; }
        std::size_t ini = pos + 6;
        if (ini < bytes.size() && bytes[ini] == '\r') ++ini;
        if (ini < bytes.size() && bytes[ini] == '\n') ++ini;
        const std::size_t fin = bytes.find("endstream", ini);
        if (fin == std::string::npos) break;
        const std::size_t dicc = bytes.rfind("<<", pos);
        const std::string diccionario = dicc == std::string::npos ? std::string() : bytes.substr(dicc, pos - dicc);
        // Imágenes y fuentes no tienen texto.
        const bool descartable = diccionario.find("/Image") != std::string::npos ||
                                 diccionario.find("/FontFile") != std::string::npos ||
                                 diccionario.find("/Length1") != std::string::npos;
        if (!descartable) {
            std::string contenido;
            if (diccionario.find("/FlateDecode") != std::string::npos) {
                std::size_t tam = 0;
                void* datos = tinfl_decompress_mem_to_heap(bytes.data() + ini, fin - ini, &tam, TINFL_FLAG_PARSE_ZLIB_HEADER);
                if (datos) {
                    contenido.assign(static_cast<const char*>(datos), tam);
                    mz_free(datos);
                }
            } else if (diccionario.find("/Filter") == std::string::npos) {
                contenido = bytes.substr(ini, fin - ini);
            }
            if (contenido.find("BT") != std::string::npos) {
                const std::string texto = textoDeContenido(contenido);
                if (texto.find_first_not_of(" \n") != std::string::npos) out += texto + "\f";
            }
        }
        pos = fin + 9;
    }
    return aUtf8(out);
}

}  // namespace funshi::agente::documentos
