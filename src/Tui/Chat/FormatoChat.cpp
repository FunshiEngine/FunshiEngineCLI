/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Formato de los mensajes del chat: descripción corta de una llamada, bloque
// del código que escribe y limpieza de llamadas dentro del texto del modelo.
#include "Tui/Chat/MonitorChat.h"

#include "Agente/Conocimiento/Lenguajes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

namespace funshi::chat {

namespace {

constexpr std::size_t kMaxDescripcion = 160;
constexpr int kMaxLineasCodigo = 60;

// Argumentos reales de la llamada (los de adentro para ejecutar_comando).
nlohmann::json argumentosDe(const std::string& herramienta, const nlohmann::json& args) {
    if (herramienta == "ejecutar_comando" && args.is_object() && args.contains("argumentos")) {
        const auto& a = args["argumentos"];
        if (a.is_object()) return a;
        if (a.is_string()) {
            const auto j = nlohmann::json::parse(a.get<std::string>(), nullptr, false);
            if (j.is_object()) return j;
        }
    }
    return args;
}

int contarLineas(const std::string& s) {
    return s.empty() ? 0 : static_cast<int>(std::count(s.begin(), s.end(), '\n')) + (s.back() == '\n' ? 0 : 1);
}

// ┌ Jugador.java · Java (.java) · Script Java del motor · 24 líneas
// │ en: src/Jugador.java
// │ resumen: …            (o "sin resumen del agente")
// │ aviso: …              (lenguaje que el motor no ejecuta como script)
// ├─
std::string cabeceraDeArchivo(const std::string& ruta, int total, const std::string& resumen) {
    namespace con = agente::conocimiento;
    const std::string nombre = ruta.empty() ? std::string("(sin nombre)") : std::filesystem::path(ruta).filename().string();
    const std::string ext = con::extensionDe(ruta);
    const con::Lenguaje* lenguaje = con::lenguajePorExtension(ext);
    std::string s = "┌ " + nombre;
    if (ext.empty()) s += " · sin extensión";
    else s += " · " + std::string(lenguaje ? lenguaje->nombre : "formato desconocido") + " (" + ext + ")";
    const std::string tipo = ruta.empty() ? std::string() : con::tipoEnMotor(ruta);
    if (!tipo.empty()) s += " · " + tipo + " del motor";
    s += " · " + std::to_string(total) + (total == 1 ? " línea" : " líneas") + "\n";
    if (!ruta.empty() && ruta != nombre) s += "│ en: " + ruta + "\n";
    s += "│ resumen: " + (resumen.empty() ? std::string("(el agente no lo indicó)") : resumen) + "\n";
    // Los headers acompañan a un script C++: no son un script aparte.
    const bool esHeader = ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx";
    if (lenguaje && lenguaje->esCodigo && !esHeader && !con::esScriptDelMotor(ruta)) {
        s += "│ aviso: el motor no ejecuta scripts " + ext + " (solo .java y .cpp)\n";
    }
    return s + "├─\n";
}

}  // namespace

std::string recortarTexto(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t corte = max;
    while (corte > 0 && (static_cast<unsigned char>(s[corte]) & 0xC0) == 0x80) --corte;  // no partir UTF-8
    return s.substr(0, corte) + "…";
}

std::string sinEspaciosExtremos(const std::string& s) {
    const auto ini = s.find_first_not_of(" \n\r\t");
    if (ini == std::string::npos) return {};
    return s.substr(ini, s.find_last_not_of(" \n\r\t") - ini + 1);
}

std::string describirLlamada(const std::string& herramienta, const std::string& argsJson) {
    auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
    // El contenido de un archivo se muestra aparte, en su bloque de código.
    const auto resumirContenido = [](nlohmann::json& a) {
        if (a.is_object() && a.contains("content") && a["content"].is_string()) {
            a["content"] = "…" + std::to_string(contarLineas(a["content"].get<std::string>())) + " líneas";
        }
        if (a.is_object() && a.contains("content")) {  // el resumen va en la cabecera del bloque
            for (const char* k : {"resumen", "summary", "descripcion", "description"}) a.erase(k);
        }
    };
    resumirContenido(args);
    if (args.is_object() && args.contains("argumentos")) resumirContenido(args["argumentos"]);
    std::string s;
    if (herramienta == "ejecutar_comando" && args.is_object() && args.contains("comando")) {
        s = args["comando"].is_string() ? args["comando"].get<std::string>() : args["comando"].dump();
        if (args.contains("argumentos")) s += " " + args["argumentos"].dump();
    } else if (herramienta == "buscar_comandos" && args.is_object() && args.contains("consulta")) {
        s = "buscar_comandos «" + (args["consulta"].is_string() ? args["consulta"].get<std::string>()
                                                                 : args["consulta"].dump()) + "»";
    } else if (herramienta == "lanzar_subagentes" && args.is_object() && args.contains("tareas") &&
               args["tareas"].is_array()) {
        s = "lanzar_subagentes · " + std::to_string(args["tareas"].size()) + " tarea(s)";
    } else {
        s = herramienta;
        if (args.is_object() && !args.empty()) s += " " + args.dump();
        else if (args.is_discarded() && !argsJson.empty()) s += " " + argsJson;
    }
    return recortarTexto(s, kMaxDescripcion);
}

std::string bloqueDeCodigo(const std::string& herramienta, const std::string& argsJson, std::string* completo) {
    const auto args = argumentosDe(herramienta, nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false));
    if (!args.is_object() || !args.contains("content") || !args["content"].is_string()) return {};
    const std::string contenido = args["content"].get<std::string>();
    if (contenido.empty()) return {};
    std::string ruta;
    for (const char* k : {"path", "name", "file"}) {
        if (args.contains(k) && args[k].is_string()) {
            ruta = args[k].get<std::string>();
            break;
        }
    }
    // El resumen puede venir junto al comando o dentro de sus argumentos.
    const auto todos = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
    std::string resumen;
    for (const auto* fuente : {&args, &todos}) {
        for (const char* k : {"resumen", "summary", "descripcion", "description"}) {
            if (resumen.empty() && fuente->is_object() && fuente->contains(k) && (*fuente)[k].is_string()) {
                resumen = (*fuente)[k].get<std::string>();
            }
        }
    }
    const int total = contarLineas(contenido);
    std::string cuerpo, recortado;
    int n = 0;
    std::size_t pos = 0;
    while (pos <= contenido.size() && n < total) {
        auto fin = contenido.find('\n', pos);
        if (fin == std::string::npos) fin = contenido.size();
        std::string linea = contenido.substr(pos, fin - pos);
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        std::string sinTab;
        for (char c : linea) sinTab += c == '\t' ? std::string("    ") : std::string(1, c);
        cuerpo += "│ " + sinTab + "\n";
        if (++n == kMaxLineasCodigo) recortado = cuerpo;
        pos = fin + 1;
    }
    const std::string cabecera = cabeceraDeArchivo(ruta, total, resumen);
    if (completo) *completo = cuerpo + "└";
    if (total > kMaxLineasCodigo) {
        return cabecera + recortado + "└ … " + std::to_string(total - kMaxLineasCodigo) +
               " líneas más (clic para ver todo)";
    }
    return cabecera + cuerpo + "└";
}

std::string quitarBloquesLlamada(const std::string& texto) {
    std::string s = texto;
    for (const char* cerco : {"```tool_call", "```json"}) {
        std::size_t pos;
        while ((pos = s.find(cerco)) != std::string::npos) {
            const auto cierre = s.find("```", pos + 3);
            const std::string bloque = s.substr(pos, cierre == std::string::npos ? std::string::npos : cierre + 3 - pos);
            // ```json solo se quita si es una llamada; un JSON explicativo queda.
            if (std::string(cerco) == "```json" && bloque.find("\"tool") == std::string::npos) break;
            s.erase(pos, bloque.size());
        }
    }
    // Donde estaba el bloque quedan renglones vacíos: como mucho uno seguido.
    std::string compacto;
    for (char c : s) {
        if (c == '\n' && compacto.size() >= 2 && compacto[compacto.size() - 1] == '\n' &&
            compacto[compacto.size() - 2] == '\n') {
            continue;
        }
        compacto += c;
    }
    s = sinEspaciosExtremos(compacto);
    // Llamada escrita sin cerco: el texto es un objeto JSON, a lo sumo
    // precedido de una etiqueta corta ("tool_call", "json").
    const auto llave = s.find('{');
    if (llave != std::string::npos && llave <= 16 && s.back() == '}' &&
        s.substr(0, llave).find_first_of(".!?") == std::string::npos &&
        !nlohmann::json::parse(s.substr(llave), nullptr, false).is_discarded()) {
        return {};
    }
    return s;
}

}  // namespace funshi::chat
