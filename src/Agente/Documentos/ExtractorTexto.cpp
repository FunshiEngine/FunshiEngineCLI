/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Extracción de texto: despacho por extensión, texto plano, HTML/XML y
// formatos ZIP. El PDF está en ExtractorTexto_Pdf.cpp.
#include "Agente/Documentos/ExtractorTexto.h"

#include "Agente/Conocimiento/Lenguajes.h"

#include <miniz/miniz.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <vector>

namespace funshi::agente::documentos {

namespace fs = std::filesystem;

std::string textoDePdfConHerramienta(const fs::path& archivo, std::string& metodo);  // _Pdf.cpp

namespace {

constexpr std::uintmax_t kMaxBytes = 64ull * 1024 * 1024;

const std::set<std::string>& extensionesTexto() {
    static const std::set<std::string> s = {".txt", ".md", ".markdown", ".rst", ".csv", ".tsv", ".log", ".tex"};
    return s;
}

const std::set<std::string>& extensionesZip() {
    static const std::set<std::string> s = {".docx", ".pptx", ".xlsx", ".odt", ".odp", ".ods", ".epub"};
    return s;
}

bool leerArchivo(const fs::path& archivo, std::string& out, std::string& error) {
    std::error_code ec;
    const auto tam = fs::file_size(archivo, ec);
    if (ec) { error = "no se pudo leer"; return false; }
    if (tam > kMaxBytes) { error = "archivo demasiado grande (más de 64 MB)"; return false; }
    std::ifstream in(archivo, std::ios::binary);
    if (!in) { error = "no se pudo abrir"; return false; }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool pareceBinario(const std::string& s) {
    const std::size_t n = std::min<std::size_t>(s.size(), 8192);
    return std::find(s.begin(), s.begin() + static_cast<long>(n), '\0') != s.begin() + static_cast<long>(n);
}

std::string decodificarEntidades(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        const auto fin = s.find(';', i);
        if (fin == std::string::npos || fin - i > 10) { out += s[i]; continue; }
        const std::string e = s.substr(i + 1, fin - i - 1);
        unsigned long cp = 0;
        if (e == "amp") cp = '&';
        else if (e == "lt") cp = '<';
        else if (e == "gt") cp = '>';
        else if (e == "quot") cp = '"';
        else if (e == "apos") cp = '\'';
        else if (e == "nbsp") cp = 0xA0;
        else if (!e.empty() && e[0] == '#') cp = std::strtoul(e.c_str() + (e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? 2 : 1), nullptr, e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? 16 : 10);
        if (cp == 0) { out += s[i]; continue; }
        // A UTF-8.
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        i = fin;
    }
    return out;
}

// Archivos de contenido dentro de un ZIP, según el formato.
std::vector<std::string> entradasDeContenido(mz_zip_archive& zip, const std::string& ext) {
    std::vector<std::string> nombres;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        char nombre[512];
        mz_zip_reader_get_filename(&zip, i, nombre, sizeof(nombre));
        const std::string s = nombre;
        const bool sirve =
            (ext == ".docx" && (s == "word/document.xml" || s.rfind("word/footnotes", 0) == 0)) ||
            (ext == ".pptx" && s.rfind("ppt/slides/slide", 0) == 0 && s.size() > 4 && s.substr(s.size() - 4) == ".xml") ||
            (ext == ".xlsx" && s == "xl/sharedStrings.xml") ||
            ((ext == ".odt" || ext == ".odp" || ext == ".ods") && s == "content.xml") ||
            (ext == ".epub" && (s.find(".xhtml") != std::string::npos || s.find(".html") != std::string::npos));
        if (sirve) nombres.push_back(s);
    }
    // Diapositivas y capítulos en orden natural (slide2 antes que slide10).
    std::sort(nombres.begin(), nombres.end(), [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return nombres;
}

TextoExtraido extraerZip(const fs::path& archivo, const std::string& ext) {
    TextoExtraido r;
    r.metodo = ext.substr(1);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archivo.string().c_str(), 0)) {
        r.error = "no es un archivo " + r.metodo + " válido";
        return r;
    }
    for (const auto& nombre : entradasDeContenido(zip, ext)) {
        std::size_t tam = 0;
        void* datos = mz_zip_reader_extract_file_to_heap(&zip, nombre.c_str(), &tam, 0);
        if (!datos) continue;
        r.texto += xmlATexto(std::string(static_cast<const char*>(datos), tam));
        r.texto += ext == ".pptx" || ext == ".epub" ? "\f" : "\n";  // una "página" por diapositiva/capítulo
        mz_free(datos);
    }
    mz_zip_reader_end(&zip);
    r.ok = !r.texto.empty();
    if (!r.ok) r.error = "sin texto";
    return r;
}

}  // namespace

std::string aUtf8(const std::string& bytes) {
    // ¿UTF-8 válido? Si no, se interpreta como Latin-1.
    bool valido = true;
    for (std::size_t i = 0; i < bytes.size() && valido;) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        const int largo = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (largo == 0 || i + largo > bytes.size()) { valido = false; break; }
        for (int k = 1; k < largo; ++k) valido = valido && (static_cast<unsigned char>(bytes[i + k]) & 0xC0) == 0x80;
        i += largo;
    }
    if (valido) return bytes;
    std::string out;
    for (unsigned char c : bytes) {
        if (c < 0x80) out += static_cast<char>(c);
        else { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::string xmlATexto(const std::string& xml) {
    // Sin scripts ni estilos; fin de párrafo, fila, celda o salto → línea nueva.
    static const std::regex bloquesOcultos(R"(<(script|style)[\s\S]*?</(script|style)>)", std::regex::icase);
    std::string s = std::regex_replace(xml, bloquesOcultos, " ");
    std::string out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '<') { out += s[i]; continue; }
        const auto fin = s.find('>', i);
        if (fin == std::string::npos) break;
        std::string tag = s.substr(i + 1, fin - i - 1);
        std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool salto = tag.rfind("/w:p", 0) == 0 || tag.rfind("/a:p", 0) == 0 || tag.rfind("/text:p", 0) == 0 ||
                           tag.rfind("/text:h", 0) == 0 || tag.rfind("/p", 0) == 0 || tag.rfind("br", 0) == 0 ||
                           tag.rfind("/div", 0) == 0 || tag.rfind("/li", 0) == 0 || tag.rfind("/tr", 0) == 0 ||
                           tag.rfind("/si", 0) == 0 || (tag.size() == 3 && tag[0] == '/' && tag[1] == 'h') ||
                           tag.rfind("w:br", 0) == 0 || tag.rfind("w:tab", 0) == 0;
        out += salto ? '\n' : (tag.rfind("/w:t", 0) == 0 || tag.rfind("/td", 0) == 0 ? ' ' : '\0');
        if (out.back() == '\0') out.pop_back();
        i = fin;
    }
    out = decodificarEntidades(out);
    // Espacios y líneas en blanco de más.
    std::string limpio;
    int vacias = 0;
    std::size_t pos = 0;
    while (pos <= out.size()) {
        auto finLinea = out.find('\n', pos);
        if (finLinea == std::string::npos) finLinea = out.size();
        std::string linea = out.substr(pos, finLinea - pos);
        linea = std::regex_replace(linea, std::regex(R"([ \t\r]+)"), " ");
        const auto ini = linea.find_first_not_of(' ');
        linea = ini == std::string::npos ? std::string() : linea.substr(ini, linea.find_last_not_of(' ') - ini + 1);
        if (linea.empty()) { if (++vacias == 1 && !limpio.empty()) limpio += '\n'; }
        else { vacias = 0; limpio += linea + '\n'; }
        pos = finLinea + 1;
    }
    return limpio;
}

bool esExtraible(const fs::path& archivo) {
    const std::string ext = conocimiento::extensionDe(archivo.string());
    return ext == ".pdf" || ext == ".html" || ext == ".htm" || ext == ".xml" || extensionesTexto().count(ext) ||
           extensionesZip().count(ext) || conocimiento::lenguajePorExtension(ext) != nullptr;
}

TextoExtraido extraerTexto(const fs::path& archivo) {
    TextoExtraido r;
    const std::string ext = conocimiento::extensionDe(archivo.string());
    if (!esExtraible(archivo)) {
        r.error = "formato no soportado (" + (ext.empty() ? std::string("sin extensión") : ext) + ")";
        return r;
    }
    if (extensionesZip().count(ext)) return extraerZip(archivo, ext);
    if (ext == ".pdf") {
        r.texto = textoDePdfConHerramienta(archivo, r.metodo);
        const bool sinHerramienta = r.metodo.empty();
        if (r.texto.find_first_not_of(" \n\f\t\r") == std::string::npos) {
            std::string bytes;
            if (!leerArchivo(archivo, bytes, r.error)) return r;
            r.texto = textoDePdf(bytes);
            r.metodo = "pdf-interno";
        }
        r.ok = r.texto.find_first_not_of(" \n\f\t\r") != std::string::npos;
        if (!r.ok) {
            // Sin pdftotext, el extractor propio no lee fuentes incrustadas (CID).
            r.error = sinHerramienta ? "no se pudo leer el PDF: instala pdftotext (poppler-utils) para PDFs con fuentes incrustadas"
                                     : "el PDF no tiene texto extraíble (¿escaneado?)";
        }
        return r;
    }
    std::string bytes;
    if (!leerArchivo(archivo, bytes, r.error)) return r;
    if (pareceBinario(bytes)) {
        r.error = "parece un archivo binario";
        return r;
    }
    bytes = aUtf8(bytes);
    if (ext == ".html" || ext == ".htm" || ext == ".xml") {
        r.texto = xmlATexto(bytes);
        r.metodo = ext == ".xml" ? "xml" : "html";
    } else {
        r.texto = std::move(bytes);
        r.metodo = "texto";
    }
    r.ok = r.texto.find_first_not_of(" \n\t\r") != std::string::npos;
    if (!r.ok) r.error = "archivo vacío";
    return r;
}

}  // namespace funshi::agente::documentos
