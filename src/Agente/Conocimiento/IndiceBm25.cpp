/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Conocimiento/IndiceBm25.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace funshi::agente::conocimiento {

namespace {

constexpr double kK1 = 1.2;
constexpr double kB = 0.75;

// Pliega a ASCII las letras acentuadas del español (UTF-8 de 2 bytes).
std::string plegar(const std::string& texto) {
    std::string out;
    out.reserve(texto.size());
    for (std::size_t i = 0; i < texto.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(texto[i]);
        if (c == 0xC3 && i + 1 < texto.size()) {
            const unsigned char d = static_cast<unsigned char>(texto[++i]);
            switch (d) {
                case 0xA1: case 0x81: out += 'a'; break;  // á Á
                case 0xA9: case 0x89: out += 'e'; break;  // é É
                case 0xAD: case 0x8D: out += 'i'; break;  // í Í
                case 0xB3: case 0x93: out += 'o'; break;  // ó Ó
                case 0xBA: case 0x9A: case 0xBC: case 0x9C: out += 'u'; break;  // ú Ú ü Ü
                case 0xB1: case 0x91: out += 'n'; break;  // ñ Ñ
                default: out += ' '; break;
            }
            continue;
        }
        out += c < 0x80 ? static_cast<char>(std::tolower(c)) : ' ';
    }
    return out;
}

const std::unordered_set<std::string>& palabrasVacias() {
    static const std::unordered_set<std::string> v = {
        "el", "la", "los", "las", "un", "una", "unos", "unas", "de", "del", "al", "a", "en",
        "y", "o", "u", "que", "se", "lo", "le", "les", "por", "para", "con", "sin", "su", "sus",
        "mi", "mis", "tu", "es", "son", "esta", "este", "esto", "como", "quiero", "puedes",
        "podes", "me", "favor", "hace", "haz", "the", "of", "to", "and", "or", "in", "on",
        "is", "si", "no", "todo", "toda", "todos", "todas", "ya", "hay", "cual", "cuales",
        // conversación: sin esto un saludo coincide con ejemplos como content="hola"
        "hola", "buenas", "bueno", "buenos", "dia", "tarde", "noche", "gracias", "ok", "vale",
        "chau", "adios", "estas", "estoy", "esta", "eso", "algo", "bien", "mal", "hubo", "ser",
        "sea", "fue", "tiene", "tengo", "quieres", "necesito",
    };
    return v;
}

// Sinónimos en español → términos del vocabulario de los comandos.
const std::unordered_map<std::string, std::vector<std::string>>& sinonimos() {
    static const std::unordered_map<std::string, std::vector<std::string>> s = {
        {"mover", {"position", "settransform"}}, {"mueve", {"position", "settransform"}},
        {"muevo", {"position", "settransform"}}, {"posicion", {"position", "settransform"}},
        {"ubicar", {"position", "settransform"}}, {"colocar", {"position", "settransform"}},
        {"poner", {"position", "settransform"}}, {"desplazar", {"position", "settransform"}},
        {"rotar", {"rotation", "settransform"}}, {"girar", {"rotation", "settransform"}},
        {"rotacion", {"rotation", "settransform"}}, {"escalar", {"scale", "settransform"}},
        {"escala", {"scale", "settransform"}}, {"tamano", {"scale", "settransform"}},
        {"agrandar", {"scale", "settransform"}}, {"achicar", {"scale", "settransform"}},
        {"transformacion", {"transform", "settransform"}},
        {"crear", {"create"}}, {"crea", {"create"}}, {"nuevo", {"create"}}, {"nueva", {"create"}},
        {"agregar", {"addcomponent", "add"}}, {"agrega", {"addcomponent", "add"}},
        {"anadir", {"addcomponent", "add"}}, {"borrar", {"delete", "remove"}},
        {"eliminar", {"delete", "remove"}}, {"quitar", {"remove", "removecomponent"}},
        {"remover", {"remove", "removecomponent"}}, {"renombrar", {"rename"}},
        {"listar", {"list"}}, {"lista", {"list"}}, {"mostrar", {"list", "get"}},
        {"ver", {"list", "get"}}, {"obtener", {"get"}}, {"leer", {"get"}},
        {"padre", {"parent", "setparent"}}, {"hijo", {"setparent", "parent"}},
        {"emparentar", {"setparent"}}, {"desvincular", {"unparent"}},
        {"desemparentar", {"unparent"}}, {"componente", {"component", "components"}},
        {"etiqueta", {"tag", "settag"}}, {"nombre", {"name", "findbyname"}},
        {"buscar", {"find", "findbyname", "findbytag"}}, {"encontrar", {"find"}},
        {"guardar", {"save", "autosave"}}, {"deshacer", {"undo"}}, {"rehacer", {"redo"}},
        {"compilar", {"compile"}}, {"recargar", {"reload"}}, {"importar", {"import"}},
        {"recurso", {"asset"}}, {"carpeta", {"dir", "fs"}}, {"directorio", {"dir", "fs"}},
        {"archivo", {"file", "src"}}, {"fichero", {"file", "src"}}, {"escena", {"scene"}},
        {"proyecto", {"project"}}, {"objeto", {"object"}}, {"entidad", {"object"}},
        {"activar", {"setactive", "active"}}, {"desactivar", {"setactive", "active"}},
        {"ocultar", {"setactive", "active"}}, {"codigo", {"script"}},
        {"comportamiento", {"script"}}, {"modelo", {"model"}}, {"malla", {"model", "mesh"}},
        {"sonido", {"audiosource", "audio"}}, {"musica", {"audiosource", "audio"}},
        {"fisica", {"rigidbody", "collider"}}, {"gravedad", {"rigidbody"}},
        {"cuerpo", {"rigidbody"}}, {"colision", {"collider"}}, {"colisionador", {"collider"}},
        {"diagnostico", {"diagnostics", "list"}}, {"error", {"diagnostics", "list"}}, {"log", {"diagnostics"}},
        {"configuracion", {"config"}}, {"ruta", {"config", "path"}},
        {"campo", {"setfield", "field"}}, {"variable", {"setfield", "field"}},
        {"propiedad", {"setfield", "field"}}, {"jerarquia", {"hierarchy"}},
        {"arbol", {"hierarchy"}}, {"mundo", {"global"}}, {"clonar", {"clone"}},
        {"duplicar", {"clone"}}, {"copiar", {"clone"}}, {"ayuda", {"help"}},
    };
    return s;
}

std::string singular(const std::string& t) {
    if (t.size() > 4 && t.back() == 's' && t[t.size() - 2] != 's') {
        return t.substr(0, t.size() - 1);
    }
    return t;
}

}  // namespace

std::vector<std::string> IndiceBm25::tokenizar(const std::string& texto) {
    const std::string plano = plegar(texto);
    std::vector<std::string> tokens;
    std::string actual;
    const auto cerrar = [&]() {
        if (actual.size() >= 2 && !palabrasVacias().count(actual) &&
            !palabrasVacias().count(singular(actual))) {
            tokens.push_back(singular(actual));
            // "settransform" → "transform", "getglobalpos" → "globalpos".
            if (actual.size() > 6 && (actual.rfind("set", 0) == 0 || actual.rfind("get", 0) == 0)) {
                tokens.push_back(actual.substr(3));
            }
            if (actual.rfind("findby", 0) == 0 && actual.size() > 6) {
                tokens.push_back("find");
                tokens.push_back(actual.substr(6));
            }
        }
        actual.clear();
    };
    for (char c : plano) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            actual += c;
        } else {
            cerrar();
        }
    }
    cerrar();
    return tokens;
}

namespace {

// Raíz para comparar conjugaciones: "rotar" → "rot" (coincide con "rota",
// "rotalo"), "hijo" → "hij" ("hija"), "error" → "erro" ("errores").
std::string raiz(const std::string& p) {
    if (p.size() > 4 && (p.compare(p.size() - 2, 2, "ar") == 0 || p.compare(p.size() - 2, 2, "er") == 0 ||
                         p.compare(p.size() - 2, 2, "ir") == 0)) {
        return p.substr(0, p.size() - 2);
    }
    return p.size() > 3 ? p.substr(0, p.size() - 1) : p;
}

}  // namespace

std::vector<std::string> IndiceBm25::expandirConsulta(const std::string& consulta) {
    std::vector<std::string> tokens = tokenizar(consulta);
    const std::size_t originales = tokens.size();
    for (std::size_t i = 0; i < originales; ++i) {
        const std::string& t = tokens[i];
        auto it = sinonimos().find(t);
        if (it == sinonimos().end()) {
            // Conjugaciones y plurales: el token empieza con la raíz de la clave.
            for (auto c = sinonimos().begin(); c != sinonimos().end(); ++c) {
                const std::string r = raiz(c->first);
                if (r.size() >= 3 && r != c->first && t.size() >= r.size() && t.compare(0, r.size(), r) == 0) {
                    it = c;
                    break;
                }
            }
        }
        if (it != sinonimos().end()) {
            tokens.insert(tokens.end(), it->second.begin(), it->second.end());
        }
    }
    return tokens;
}

std::size_t IndiceBm25::agregar(const std::string& texto) {
    Documento doc;
    for (const auto& t : tokenizar(texto)) {
        ++doc.frecuencias[t];
        ++doc.largo;
    }
    documentos_.push_back(std::move(doc));
    return documentos_.size() - 1;
}

void IndiceBm25::construir() {
    idf_.clear();
    std::unordered_map<std::string, int> enCuantos;
    double total = 0.0;
    for (const auto& d : documentos_) {
        total += static_cast<double>(d.largo);
        for (const auto& [t, _] : d.frecuencias) ++enCuantos[t];
    }
    const double n = static_cast<double>(documentos_.size());
    largoPromedio_ = documentos_.empty() ? 0.0 : total / n;
    for (const auto& [t, df] : enCuantos) {
        idf_[t] = std::log(1.0 + (n - df + 0.5) / (df + 0.5));
    }
}

std::vector<std::pair<std::size_t, double>> IndiceBm25::buscar(const std::string& consulta,
                                                               std::size_t k) const {
    const auto terminos = expandirConsulta(consulta);
    std::vector<std::pair<std::size_t, double>> puntajes;
    for (std::size_t i = 0; i < documentos_.size(); ++i) {
        const Documento& d = documentos_[i];
        double puntaje = 0.0;
        for (const auto& t : terminos) {
            const auto f = d.frecuencias.find(t);
            if (f == d.frecuencias.end()) continue;
            const auto idf = idf_.find(t);
            const double tf = f->second;
            const double norm = 1.0 - kB + kB * (largoPromedio_ > 0 ? d.largo / largoPromedio_ : 1.0);
            puntaje += (idf == idf_.end() ? 0.0 : idf->second) * tf * (kK1 + 1.0) / (tf + kK1 * norm);
        }
        if (puntaje > 0.0) puntajes.emplace_back(i, puntaje);
    }
    std::sort(puntajes.begin(), puntajes.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (puntajes.size() > k) puntajes.resize(k);
    return puntajes;
}

}  // namespace funshi::agente::conocimiento
