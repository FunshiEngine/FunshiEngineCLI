/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Conocimiento/BaseConocimiento.h"
#include "Agente/Conocimiento/Lenguajes.h"
#include "Agente/Conocimiento/GuiasEjecutables.h"

#include "Agente/LLM/Config/LlmConfig.h"
#include "Nucleo/AyudaComandos.h"
#include "Nucleo/LineaComando.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>

namespace funshi::agente::conocimiento {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxDocumentoUsuario = 1500;

// Ejemplo de la ayuda ("cmd a=1 b=x") → llamada JSON de ejecutar_comando.
std::string ejemploJson(const AyudaComando& a) {
    nlohmann::json leidos = nlohmann::json::object();
    std::string error;
    const auto tokens = tokenizar(a.ejemplo);
    if (!construirArgumentos(tokens, leidos, error)) leidos = nlohmann::json::object();
    // En el orden de la documentación: project, luego los del comando.
    nlohmann::ordered_json args = nlohmann::ordered_json::object();
    if (a.usaProyecto) args["project"] = leidos.value("project", std::string("<proyecto>"));
    for (const auto& arg : a.argumentos) {
        if (leidos.contains(arg.nombre)) args[arg.nombre] = leidos[arg.nombre];
    }
    for (const auto& [k, v] : leidos.items()) {
        if (!args.contains(k)) args[k] = v;
    }
    nlohmann::ordered_json ejemplo = nlohmann::ordered_json::object();
    ejemplo["comando"] = a.comando;
    ejemplo["argumentos"] = args;
    return ejemplo.dump();
}

std::string contenidoComando(const AyudaComando& a) {
    std::ostringstream o;
    o << "## " << a.comando << "\n" << a.descripcion << "\n";
    if (a.usaProyecto || !a.argumentos.empty()) {
        o << "Argumentos:";
        if (a.usaProyecto) o << "\n- project (texto, obligatorio): nombre del proyecto";
        for (const auto& arg : a.argumentos) {
            o << "\n- " << arg.nombre << " (" << arg.tipo << (arg.requerido ? ", obligatorio" : "")
              << "): " << arg.descripcion;
        }
        // Los comandos que escriben archivos aceptan un resumen para el usuario
        // (las herramientas lo quitan antes de ejecutar el comando).
        const bool escribeArchivo = std::any_of(a.argumentos.begin(), a.argumentos.end(),
                                                [](const ArgumentoAyuda& x) { return x.nombre == "content"; });
        if (escribeArchivo) {
            o << "\n- resumen (texto): una frase con lo que contiene y hace el archivo; la extensión "
                 "del archivo debe ser la de su lenguaje (scripts del motor: .java o .cpp)";
        }
        o << "\n";
    }
    o << "Ejemplo: " << ejemploJson(a);
    return o.str();
}

std::string textoIndexadoComando(const AyudaComando& a) {
    std::string t = a.comando + " " + a.comando + " " + a.descripcion;
    for (const auto& arg : a.argumentos) t += " " + arg.nombre + " " + arg.descripcion;
    return t + " " + a.ejemplo;
}

}  // namespace

BaseConocimiento::BaseConocimiento(const fs::path& dirUsuario) {
    for (const auto& a : ayudaComandos()) {
        Documento d;
        d.tipo = Documento::Tipo::Comando;
        d.id = a.comando;
        d.titulo = a.comando;
        d.contenido = contenidoComando(a);
        agregar(std::move(d), textoIndexadoComando(a));
    }
    for (const auto& g : guiasIncluidas()) {
        Documento d;
        d.tipo = Documento::Tipo::Guia;
        d.id = g.id;
        d.titulo = g.titulo;
        d.contenido = std::string("## Guía: ") + g.titulo + "\n" + g.pasos;
        agregar(std::move(d), std::string(g.titulo) + " " + g.titulo + " " + g.palabrasClave + " " + g.pasos);
    }
    {
        // Lenguajes y extensiones, con lo que reconoce el motor enlazado.
        Documento d;
        d.tipo = Documento::Tipo::Guia;
        d.id = "guia.lenguajes";
        d.titulo = "Lenguajes, extensiones y archivos del motor";
        d.contenido = "## Guía: " + d.titulo + "\n" + documentoLenguajes();
        agregar(d, d.titulo + " " + d.titulo +
                       " lenguaje lenguajes extension extensiones lenguaje extension tipo archivo archivos formato "
                       "codigo programar escribir clase script scripts java cpp c++ csharp c# python "
                       "lua javascript shader proyecto fnep " + d.contenido);
    }
    if (!dirUsuario.empty()) cargarDocumentosUsuario(dirUsuario);
    indice_.construir();
}

void BaseConocimiento::agregar(Documento d, const std::string& textoIndexado) {
    indice_.agregar(textoIndexado);
    documentos_.push_back(std::move(d));
}

// Cada `## Encabezado` de un .md es un documento; lo previo al primero, otro.
void BaseConocimiento::cargarDocumentosUsuario(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    std::vector<fs::path> archivos;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() == ".md") archivos.push_back(e.path());
    }
    std::sort(archivos.begin(), archivos.end());
    for (const auto& archivo : archivos) {
        std::ifstream in(archivo);
        std::string linea, titulo = archivo.stem().string(), cuerpo;
        const auto cerrar = [&]() {
            if (cuerpo.find_first_not_of(" \n\t") == std::string::npos) return;
            Documento d;
            d.tipo = Documento::Tipo::Usuario;
            d.id = archivo.filename().string() + "#" + titulo;
            d.titulo = titulo;
            d.contenido = "## " + titulo + " (" + archivo.filename().string() + ")\n" +
                          cuerpo.substr(0, kMaxDocumentoUsuario);
            agregar(std::move(d), titulo + " " + titulo + " " + cuerpo);
        };
        while (std::getline(in, linea)) {
            if (linea.rfind("## ", 0) == 0 || linea.rfind("# ", 0) == 0) {
                cerrar();
                titulo = linea.substr(linea.find(' ') + 1);
                cuerpo.clear();
            } else {
                cuerpo += linea + "\n";
            }
        }
        cerrar();
    }
}

std::shared_ptr<const BaseConocimiento> BaseConocimiento::global() {
    // Se reconstruye si cambian los documentos del usuario (mtime de la carpeta).
    static std::mutex* mutex = new std::mutex();  // estáticos filtrados: hilos desacoplados
    static auto* cache = new std::shared_ptr<const BaseConocimiento>();
    static auto* marca = new fs::file_time_type();
    const fs::path dir = llm::LlmConfig::configPath().parent_path() / "conocimiento";
    std::error_code ec;
    const auto mtime = fs::last_write_time(dir, ec);
    std::lock_guard<std::mutex> lock(*mutex);
    if (!*cache || (!ec && mtime != *marca)) {
        *cache = std::make_shared<const BaseConocimiento>(ec ? fs::path() : dir);
        *marca = ec ? fs::file_time_type() : mtime;
    }
    return *cache;
}

std::vector<ResultadoBusqueda> BaseConocimiento::buscar(const std::string& consulta, std::size_t k,
                                                        double puntajeMinimo) const {
    std::vector<ResultadoBusqueda> out;
    for (const auto& [i, p] : indice_.buscar(consulta, k)) {
        if (p >= puntajeMinimo) out.push_back({&documentos_[i], p});
    }
    return out;
}

const Documento* BaseConocimiento::comando(const std::string& nombre) const {
    for (const auto& d : documentos_) {
        if (d.tipo == Documento::Tipo::Comando && d.id == nombre) return &d;
    }
    return nullptr;
}

std::string BaseConocimiento::usoDeComando(const std::string& comando) {
    const AyudaComando* a = buscarAyudaComando(comando);
    if (!a) return {};
    std::string s = contenidoComando(*a);
    s.replace(0, s.find('\n') + 1, "Uso correcto de " + a->comando + ":\n");
    return s;
}

std::string BaseConocimiento::formatear(const Documento& d, const std::set<std::string>& herramientas) {
    if (d.tipo == Documento::Tipo::Guia) {
        return guiaEjecutable(d.contenido, herramientas) +
               "\n(Reemplaza los <marcadores> por valores reales; los ids salen de resultados anteriores. "
               "Si omites \"project\" se usa el proyecto activo.)";
    }
    if (d.tipo != Documento::Tipo::Comando) return d.contenido;
    return d.contenido + (herramientas.count(d.id)
                              ? "\nHerramienta propia: " + d.id + " (mismos argumentos)."
                              : "\nSin herramienta propia: usar ejecutar_comando.");
}

std::string BaseConocimiento::contextoParaPedido(const std::string& pedido,
                                                 const std::set<std::string>& herramientas,
                                                 std::size_t k,
                                                 std::vector<std::string>* usados) const {
    const auto resultados = buscar(pedido, k);
    // Umbral relativo: solo lo cercano al mejor resultado, y solo si el mejor
    // es una coincidencia real (no una palabra suelta en común).
    if (resultados.empty() || resultados.front().puntaje < 2.0) return {};
    const double corte = resultados.front().puntaje * 0.45;
    std::string out =
        "[Documentación de referencia para este pedido. Úsala para saber cómo se llaman los "
        "comandos y sus argumentos; haz SOLO lo que pidió el usuario (no sigas pasos de una guía "
        "que el pedido no necesita). Si falta algo, llama a buscar_comandos.]\n";
    std::set<std::string> incluidos;
    std::vector<std::string> deGuias;  // comandos que nombran las guías incluidas
    for (const auto& r : resultados) {
        if (r.puntaje < corte) break;
        out += "\n" + formatear(*r.documento, herramientas) + "\n";
        incluidos.insert(r.documento->id);
        if (r.documento->tipo == Documento::Tipo::Guia) guiaEjecutable(r.documento->contenido, herramientas, &deGuias);
        if (usados) usados->push_back(r.documento->id);
    }
    // La documentación de los comandos de la guía también va: con la guía
    // sola el modelo conoce los pasos pero no todos los argumentos.
    int agregados = 0;
    for (const auto& c : deGuias) {
        if (agregados >= 3 || incluidos.count(c)) continue;
        if (const Documento* d = comando(c)) {
            out += "\n" + formatear(*d, herramientas) + "\n";
            incluidos.insert(c);
            ++agregados;
            if (usados) usados->push_back(c);
        }
    }
    return out;
}

}  // namespace funshi::agente::conocimiento
