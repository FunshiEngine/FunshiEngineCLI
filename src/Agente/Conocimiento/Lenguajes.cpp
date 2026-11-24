/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Conocimiento/Lenguajes.h"

#include "Assets/Tipos/TiposArchivo.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace funshi::agente::conocimiento {

namespace {

// Extensiones que se le consultan al motor para armar su sección. Incluye
// las de proyecto candidatas: si el motor reconoce alguna, se informa.
const std::vector<const char*>& candidatasMotor() {
    static const std::vector<const char*> v = {
        ".java", ".cpp", ".cc", ".cxx", ".prefab", ".fnep", ".obj", ".fbx", ".gltf", ".glb", ".dae",
        ".3ds", ".blend", ".ply", ".stl", ".x", ".md5mesh", ".png", ".jpg", ".jpeg", ".jpe", ".tga",
        ".bmp", ".psd", ".gif", ".hdr", ".wav", ".ogg", ".mp3", ".flac", ".html", ".htm"};
    return v;
}

}  // namespace

const std::vector<Lenguaje>& lenguajes() {
    static const std::vector<Lenguaje> v = {
        {"Java", {".java"}, "scripts del motor (lenguaje de script principal)", true},
        {"C++", {".cpp", ".cc", ".cxx", ".hpp", ".hh", ".hxx", ".h"}, "scripts del motor (.cpp) y código nativo", true},
        {"C", {".c", ".h"}, "código nativo", true},
        {"C#", {".cs"}, "Unity y .NET; el motor no lo ejecuta", true},
        {"Python", {".py"}, "herramientas y automatización; el motor no lo ejecuta", true},
        {"JavaScript", {".js", ".mjs"}, "páginas web e interfaces HTML", true},
        {"TypeScript", {".ts"}, "JavaScript tipado (se compila a .js)", true},
        {"Lua", {".lua"}, "scripting embebido; el motor no lo ejecuta", true},
        {"Rust", {".rs"}, "código nativo; el motor no lo ejecuta", true},
        {"Go", {".go"}, "servicios y herramientas", true},
        {"Kotlin", {".kt", ".kts"}, "JVM y Android", true},
        {"Swift", {".swift"}, "Apple", true},
        {"GLSL", {".glsl", ".vert", ".frag", ".geom", ".comp"}, "shaders de OpenGL", true},
        {"HLSL", {".hlsl"}, "shaders de DirectX", true},
        {"Shell", {".sh"}, "scripts de terminal Linux/macOS", true},
        {"PowerShell", {".ps1"}, "scripts de Windows", true},
        {"Batch", {".bat", ".cmd"}, "scripts de Windows", true},
        {"SQL", {".sql"}, "consultas a bases de datos", true},
        {"HTML", {".html", ".htm"}, "interfaces HTML del motor", false},
        {"CSS", {".css"}, "estilos de interfaces HTML", false},
        {"JSON", {".json"}, "datos; el motor lo usa para tilesets y perfiles de partículas", false},
        {"XML", {".xml"}, "datos", false},
        {"YAML", {".yaml", ".yml"}, "configuración", false},
        {"TOML", {".toml"}, "configuración", false},
        {"INI", {".ini", ".cfg"}, "configuración", false},
        {"CMake", {".cmake"}, "compilación (también CMakeLists.txt)", false},
        {"Markdown", {".md"}, "documentación", false},
        {"Texto", {".txt"}, "texto plano", false},
    };
    return v;
}

std::string extensionDe(const std::string& ruta) {
    std::string ext = std::filesystem::path(ruta).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

const Lenguaje* lenguajePorExtension(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    for (const auto& l : lenguajes()) {
        for (const char* e : l.extensiones) {
            if (ext == e) return &l;
        }
    }
    return nullptr;
}

std::string tipoEnMotor(const std::string& ruta) {
    const TipoArchivo tipo = TiposArchivo::clasificar(ruta);
    return tipo == TipoArchivo::Desconocido ? std::string() : TiposArchivo::nombre(tipo);
}

bool esScriptDelMotor(const std::string& ruta) {
    const TipoArchivo tipo = TiposArchivo::clasificar(ruta);
    return tipo == TipoArchivo::ScriptCpp || tipo == TipoArchivo::ScriptJava;
}

std::string extensionDeProyecto() {
    for (const char* ext : candidatasMotor()) {
        if (TiposArchivo::clasificar(std::string("proyecto") + ext) == TipoArchivo::Proyecto) return ext;
    }
    return {};
}

std::string documentoLenguajes() {
    std::string s = "Lenguajes y sus extensiones (elige la extensión según el lenguaje del código):\n";
    for (const auto& l : lenguajes()) {
        std::string exts;
        for (const char* e : l.extensiones) exts += (exts.empty() ? "" : " ") + std::string(e);
        s += "- " + std::string(l.nombre) + ": " + exts + " — " + l.uso + "\n";
    }

    // Archivos del motor, agrupados por el tipo que les da TiposArchivo.
    std::map<std::string, std::string> porTipo;
    for (const char* ext : candidatasMotor()) {
        const std::string tipo = tipoEnMotor(std::string("archivo") + ext);
        if (!tipo.empty() && tipo != "Proyecto") porTipo[tipo] += (porTipo[tipo].empty() ? "" : " ") + std::string(ext);
    }
    s += "\nArchivos que reconoce el motor:\n";
    for (const auto& [tipo, exts] : porTipo) s += "- " + tipo + ": " + exts + "\n";
    s += "- Tileset / Perfil de partículas: .json (según su contenido)\n"
         "- Escenas: las guarda el propio motor (SceneBBDDObjetos.txt y Scene/Object*.db); se "
         "modifican con los comandos scene.*, nunca editando esos archivos.\n";

    s += "\nScripts: el motor solo ejecuta scripts Java (.java) y C++ (.cpp). Un pedido de "
         "script en otro lenguaje (C#, Python, Lua…) se escribe en Java o C++, o se avisa al "
         "usuario que el motor no lo ejecuta. El código debe corresponder a la extensión: en un "
         ".java va Java, en un .cpp va C++.\n";

    const std::string proyecto = extensionDeProyecto();
    if (proyecto.empty()) {
        s += "\nProyectos: el motor no tiene todavía una extensión para archivos de proyecto; un "
             "proyecto es su carpeta Proyects/<nombre> y se crea con project.create.\n";
    } else {
        s += "\nProyectos: el motor reconoce " + proyecto +
             " como archivo de proyecto, pero todavía no crea ni abre esos archivos: un proyecto "
             "es su carpeta Proyects/<nombre> y se crea con project.create. No crees archivos " +
             proyecto + " a mano.\n";
    }
    return s;
}

}  // namespace funshi::agente::conocimiento
