/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Comandos/ComandosComun.h"

#include <Configuracion/ProjectManager.h>

#include "Nucleo/Registro.h"
#include "Proyectos/ResolucionProyectos.h"
#include "Validacion/ValidacionArgs.h"

namespace funshi {

namespace fs = std::filesystem;

// Resuelve args.project y el proyecto. Si falla, deja en `resultado` la
// respuesta de error y devuelve false.
bool resolverProyectoDesdeArgs(const nlohmann::json& argumentos,
                               std::string& nombreProyecto,
                               ResultadoComando& resultado) {
    std::string error;
    if (!argumentoProyecto(argumentos, nombreProyecto, error)) {
        registrarErrorUso("invalid_request", error);
        resultado = ResultadoComando::fallo("invalid_request", error,
                                            codigosSalida::kErrorUso);
        return false;
    }
    ProjectManager::Proyecto proyecto;
    if (!resolverProyectoPorNombre(nombreProyecto, proyecto, error)) {
        registrarErrorOperacional("invalid_project", error);
        resultado = ResultadoComando::fallo("invalid_project", error,
                                            codigosSalida::kProyectoInvalido);
        return false;
    }
    return true;
}

// Verifica que `destino` quede bajo `raiz` tras resolver rutas canónicas.
// Devuelve false con `resultado` en error si la ruta escapa. La comparación
// por componentes evita falsos positivos entre rutas con prefijos textuales
// comunes (p.ej. `src` y `src-otro`).
bool rutaSegura(const fs::path& raiz, const fs::path& destino,
                const std::string& mensaje, ResultadoComando& resultado) {
    std::error_code ec;
    const fs::path raizAbsoluta = fs::weakly_canonical(raiz, ec);
    const fs::path destinoAbsoluto = fs::weakly_canonical(destino, ec);
    if (ec || raizAbsoluta.empty() || destinoAbsoluto.empty()) {
        registrarErrorOperacional("invalid_path", mensaje);
        resultado = ResultadoComando::fallo("invalid_path", mensaje,
                                            codigosSalida::kRecursoNoEncontrado);
        return false;
    }

    auto raizIt = raizAbsoluta.begin();
    auto destinoIt = destinoAbsoluto.begin();
    for (; raizIt != raizAbsoluta.end(); ++raizIt, ++destinoIt) {
        if (destinoIt == destinoAbsoluto.end() || *destinoIt != *raizIt) {
            registrarErrorOperacional("invalid_path", mensaje);
            resultado = ResultadoComando::fallo("invalid_path", mensaje,
                                                codigosSalida::kRecursoNoEncontrado);
            return false;
        }
    }
    return true;
}

}  // namespace funshi
