/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// Utilidades de cli-tests: ejecutar el binario y leer sus respuestas.
#include "PruebasCli.h"


#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <array>
#include <memory>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <nlohmann/json.hpp>

#include "TempPruebas.h"

namespace pruebas_cli {

int total = 0;
int fallos = 0;

// Extrae el codigo de salida de std::system de forma multiplataforma. En
// Windows el valor devuelto ya es el exit code; en POSIX hay que desempaquetar
// el estado de espera.
int codigoSalidaSistema(int rc) {
#ifdef _WIN32
    return rc;
#else
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
}

std::string ejecutarSistema(const std::string& cmd) {
    std::array<char, 4096> buffer;
    std::string resultado;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("Error al ejecutar: " + cmd);
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        resultado += buffer.data();
    }
    return resultado;
}

// Lee un archivo de texto completo.
std::string leerArchivo(const fs::path& ruta) {
    std::ifstream fh(ruta, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(fh)),
                       std::istreambuf_iterator<char>());
}

// Parsea un JSON sin lanzar excepcion: si el contenido no es JSON valido
// devuelve un valor nulo, que los comprobadores tratan como respuesta
// ausente.
nlohmann::json leerJson(const fs::path& ruta) {
    const std::string contenido = leerArchivo(ruta);
    if (contenido.empty()) {
        return nlohmann::json();
    }
    return nlohmann::json::parse(contenido, nullptr, false);
}

// Indica si una respuesta del CLI es satisfactoria.
bool esRespuestaOk(const nlohmann::json& j) {
    return j.is_object() && j.contains("ok") && j["ok"].is_boolean() &&
           j["ok"].get<bool>();
}

// Codigo de error de una respuesta fallida, o cadena vacia.
std::string codigoError(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("error") || !j["error"].is_object()) {
        return {};
    }
    return j["error"].value("code", std::string());
}

// Construye una solicitud JSON con comando y argumentos.
nlohmann::json solicitudJson(const std::string& comando,
                             nlohmann::json args) {
    return {{"command", comando}, {"args", std::move(args)}};
}

// Ejecuta el CLI con una peticion y deja la respuesta en un archivo. Devuelve
// el codigo de salida. La redireccion se escribe entre comillas porque la
// carpeta temporal puede estar en una ruta con espacios.
int ejecutarPeticion(const fs::path& bin, const fs::path& peticion,
                     const fs::path& salida) {
    const std::string cmd = bin.string() + " --request-file \"" + peticion.string() +
                            "\" > \"" + salida.string() + "\"";
    return codigoSalidaSistema(std::system(cmd.c_str()));
}

// Ejecuta una solicitud descrita como JSON, escribiendo los archivos temporales
// bajo `directorio`. Devuelve la respuesta parseada y su codigo de salida.
nlohmann::json ejecutarSolicitudJson(const fs::path& bin,
                                     const fs::path& directorio,
                                     const std::string& nombre,
                                     const nlohmann::json& solicitud,
                                     int& codigoSalida) {
    const fs::path peticion = directorio / ("solicitud-" + nombre + ".json");
    const fs::path salida = directorio / ("respuesta-" + nombre + ".json");
    {
        std::ofstream fh(peticion);
        fh << solicitud.dump();
    }
    codigoSalida = ejecutarPeticion(bin, peticion, salida);
    return leerJson(salida);
}

// Igual que `ejecutarSolicitudJson`, pero lanza el CLI con `directorioTrabajo`
// como directorio actual. Así la configuracion persistida y los proyectos
// quedan aislados del directorio de build.
nlohmann::json ejecutarAislado(const fs::path& bin,
                               const fs::path& directorioTrabajo,
                               const fs::path& directorioTemporal,
                               const std::string& nombre,
                               const nlohmann::json& solicitud,
                               int& codigoSalida) {
    const fs::path peticion = directorioTemporal / ("solicitud-" + nombre + ".json");
    const fs::path salida = directorioTemporal / ("respuesta-" + nombre + ".json");
    {
        std::ofstream fh(peticion);
        fh << solicitud.dump();
    }
    const std::string cmd = "cd \"" + directorioTrabajo.string() + "\" && " +
                            bin.string() + " --request-file \"" + peticion.string() +
                            "\" > \"" + salida.string() + "\"";
    codigoSalida = codigoSalidaSistema(std::system(cmd.c_str()));
    return leerJson(salida);
}

// Ruta del binario del CLI: mismo directorio que este ejecutable.
void fijarVariable(const char* nombre, const std::string& valor) {
#ifdef _WIN32
    _putenv_s(nombre, valor.c_str());
#else
    setenv(nombre, valor.c_str(), 1);
#endif
}

fs::path rutaBinarioCLI(const char* argv0) {
    fs::path dir = fs::absolute(argv0).parent_path();
#ifdef _WIN32
    return dir / "FunshiCLI.exe";
#else
    return dir / "FunshiCLI";
#endif
}

}  // namespace pruebas_cli
