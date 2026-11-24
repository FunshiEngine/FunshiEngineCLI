/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Comandos/ComandosPapelera.h"

#include <Assets/AssetPath.h>
#include <Configuracion/ProjectPaths.h>
#include <Objetos/Componentes/Render/Material.h>

#include "Comandos/ComandosBorrado.h"
#include "Comandos/Escena/ComandosEscenaInterno.h"
#include "Escena/SerializacionEscena.h"
#include "Nucleo/NucleoCLI.h"
#include "Nucleo/Registro.h"
#include "Validacion/ValidacionArgs.h"

#include <chrono>
#include <ctime>
#include <fstream>

namespace funshi {

namespace fs = std::filesystem;

namespace {

fs::path rutaIndice(const std::string& proyecto) { return carpetaPapelera(proyecto) / "indice.json"; }

nlohmann::json leerIndice(const std::string& proyecto) {
    std::ifstream f(rutaIndice(proyecto));
    auto j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) j = nlohmann::json::object();
    if (!j.contains("entradas") || !j["entradas"].is_array()) j["entradas"] = nlohmann::json::array();
    if (!j.contains("siguiente") || !j["siguiente"].is_number_integer()) j["siguiente"] = 1;
    return j;
}

void escribirIndice(const std::string& proyecto, const nlohmann::json& indice) {
    const fs::path ruta = rutaIndice(proyecto);
    const fs::path tmp = ruta.string() + ".tmp";
    {
        std::ofstream f(tmp);
        f << indice.dump(2);
    }
    std::error_code ec;
    fs::rename(tmp, ruta, ec);
}

std::string ahoraIso() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return buf;
}

int anotar(const std::string& proyecto, nlohmann::json entrada) {
    auto indice = leerIndice(proyecto);
    const int id = indice["siguiente"].get<int>();
    indice["siguiente"] = id + 1;
    entrada["id"] = id;
    entrada["deletedAt"] = ahoraIso();
    indice["entradas"].push_back(std::move(entrada));
    escribirIndice(proyecto, indice);
    return id;
}

// Mover entre carpetas del mismo disco; si no se puede, copiar y borrar.
bool mover(const fs::path& desde, const fs::path& hasta) {
    std::error_code ec;
    fs::create_directories(hasta.parent_path(), ec);
    fs::rename(desde, hasta, ec);
    if (!ec) return true;
    ec.clear();
    fs::copy(desde, hasta, fs::copy_options::recursive, ec);
    if (ec) return false;
    fs::remove_all(desde, ec);
    return true;
}

// Quita del historial de deshacer el borrado de ese prefab (ya no aplica).
void olvidarBorradoEnHistorial(const std::string& proyecto, const std::string& prefab) {
    for (const std::string ruta : {rutaHistorialUndo(proyecto), rutaHistorialUndo(proyecto) + ".redo"}) {
        nlohmann::json historial;
        if (!cargarHistorial(ruta, historial)) continue;
        const auto antes = historial.size();
        historial.erase(std::remove_if(historial.begin(), historial.end(),
                                       [&](const nlohmann::json& op) {
                                           return op.value("type", "") == "delete" &&
                                                  op.value("before", nlohmann::json::object()).value("prefab", "") == prefab;
                                       }),
                        historial.end());
        if (historial.size() != antes) guardarHistorial(ruta, historial);
    }
}

// Vuelve a poner en los objetos las referencias que se quitaron al borrar.
void restaurarReferencias(const std::string& proyecto, const std::string& ruta, const nlohmann::json& referencias,
                          nlohmann::json& restauradas, nlohmann::json& sinRestaurar) {
    nlohmann::json materiales = nlohmann::json::array();
    for (const auto& r : referencias) {
        const std::string tipo = r.value("componentType", "");
        if (tipo == "Material") {
            materiales.push_back(r);
            continue;
        }
        if (tipo == "Script" || tipo == "Model" || tipo == "AudioSource") {
            Solicitud agregar;
            agregar.comando = "scene.object.addcomponent";
            agregar.argumentos = {{"project", proyecto}, {"id", static_cast<unsigned>(r.value("id", 0))}, {"componentType", tipo}, {"value", ruta}};
            (ejecutarSolicitud(agregar).respuesta.correcta ? restauradas : sinRestaurar).push_back(r);
        } else {
            sinRestaurar.push_back(r);
        }
    }
    if (materiales.empty()) return;
    ContextoEscena contexto(proyecto);
    bool cambio = false;
    for (const auto& r : materiales) {
        GameObject* obj = findObjectById(&contexto.registro(), r.value("id", -1));
        Material* material = nullptr;
        if (obj && obj->getComponents() && !obj->getComponents()->isEmpty()) {
            auto* comps = obj->getComponents();
            for (auto* p = comps->first(); p && !material; p = (p != comps->last()) ? comps->next(p) : nullptr) {
                material = dynamic_cast<Material*>(p->getElement());
            }
        }
        const std::string campo = r.value("field", "");
        if (!material) {
            sinRestaurar.push_back(r);
            continue;
        }
        if (campo == "diffuseMap") material->setDiffuseMapPath(ruta);
        else if (campo == "specularMap") material->setSpecularMapPath(ruta);
        else if (campo == "normalMap") material->setNormalMapPath(ruta);
        else if (campo == "emissionMap") material->setEmissionMapPath(ruta);
        obj->setDirty(true);
        cambio = true;
        restauradas.push_back(r);
    }
    if (cambio) guardarEscena(contexto, proyecto);
}

bool idDeArgs(const nlohmann::json& argumentos, bool requerido, int& id, ResultadoComando& resultado) {
    std::string error;
    if (!validarArgumentos(argumentos, {{"id", requerido, nlohmann::json::value_t::number_unsigned}}, error)) {
        registrarErrorUso("invalid_request", error);
        resultado = ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
        return false;
    }
    id = argumentos.contains("id") ? static_cast<int>(argumentos["id"].get<unsigned>()) : -1;
    return true;
}

}  // namespace

fs::path carpetaPapelera(const std::string& proyecto) {
    const fs::path dir = fs::path(rutaHistorialUndo(proyecto)).parent_path() / "papelera";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

int moverAPapelera(const std::string& proyecto, const std::string& rutaRelativa, const fs::path& rutaAbsoluta,
                   const std::string& tipo, const nlohmann::json& referencias, std::string& error) {
    auto indice = leerIndice(proyecto);
    const int id = indice["siguiente"].get<int>();
    const fs::path guardado = carpetaPapelera(proyecto) / std::to_string(id) / rutaAbsoluta.filename();
    if (!mover(rutaAbsoluta, guardado)) {
        error = "No se pudo mover " + rutaRelativa + " a la papelera";
        return -1;
    }
    return anotar(proyecto, {{"kind", "archivo"},
                             {"path", rutaRelativa},
                             {"type", tipo},
                             {"stored", fs::relative(guardado, carpetaPapelera(proyecto)).generic_string()},
                             {"references", referencias}});
}

int anotarObjetoEnPapelera(const std::string& proyecto, const std::string& nombre, int idObjeto, int padreId,
                           const std::string& prefab) {
    return anotar(proyecto, {{"kind", "objeto"},
                             {"name", nombre},
                             {"type", "Objeto"},
                             {"objectId", idObjeto},
                             {"parentId", padreId},
                             {"prefab", prefab}});
}

void quitarDePapeleraPorPrefab(const std::string& proyecto, const std::string& prefab) {
    auto indice = leerIndice(proyecto);
    auto& e = indice["entradas"];
    const auto antes = e.size();
    e.erase(std::remove_if(e.begin(), e.end(), [&](const nlohmann::json& x) { return x.value("prefab", "") == prefab; }),
            e.end());
    if (e.size() != antes) escribirIndice(proyecto, indice);
}

ResultadoComando papeleraListar(const nlohmann::json& argumentos) {
    std::string proyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, proyecto, resultado)) return resultado;
    const auto indice = leerIndice(proyecto);
    nlohmann::json items = nlohmann::json::array();
    for (const auto& e : indice["entradas"]) {
        nlohmann::json item = {{"id", e.value("id", 0)}, {"type", e.value("type", "")}, {"deletedAt", e.value("deletedAt", "")}};
        if (e.value("kind", "") == "objeto") item["name"] = e.value("name", "");
        else item["path"] = e.value("path", "");
        item["references"] = e.value("references", nlohmann::json::array()).size();
        items.push_back(std::move(item));
    }
    return ResultadoComando::exito({{"project", proyecto}, {"count", items.size()}, {"items", items},
                                    {"folder", carpetaPapelera(proyecto).string()}});
}

ResultadoComando papeleraRestaurar(const nlohmann::json& argumentos) {
    std::string proyecto;
    ResultadoComando resultado;
    int id = -1;
    if (!resolverProyectoDesdeArgs(argumentos, proyecto, resultado) || !idDeArgs(argumentos, true, id, resultado)) {
        return resultado;
    }
    auto indice = leerIndice(proyecto);
    auto& entradas = indice["entradas"];
    auto it = std::find_if(entradas.begin(), entradas.end(), [&](const auto& e) { return e.value("id", 0) == id; });
    if (it == entradas.end()) {
        const std::string mensaje = "No hay nada con el id " + std::to_string(id) + " en la papelera (mira trash.list)";
        registrarErrorUso("not_in_trash", mensaje);
        return ResultadoComando::fallo("not_in_trash", mensaje, codigosSalida::kRecursoNoEncontrado);
    }
    const nlohmann::json entrada = *it;
    nlohmann::json respuesta = {{"project", proyecto}, {"id", id}, {"type", entrada.value("type", "")}, {"restored", true}};
    if (entrada.value("kind", "") == "objeto") {
        ContextoEscena contexto(proyecto);
        const int nuevo = restaurarObjetoBorrado(contexto, entrada);
        if (nuevo < 0) {
            registrarErrorOperacional("restore_failed", "No se pudo restaurar el objeto de la papelera");
            return ResultadoComando::fallo("restore_failed", "No se pudo restaurar el objeto", codigosSalida::kFalloGeneral);
        }
        guardarEscena(contexto, proyecto);
        olvidarBorradoEnHistorial(proyecto, entrada.value("prefab", ""));
        std::error_code ec;
        fs::remove(entrada.value("prefab", ""), ec);
        respuesta["name"] = entrada.value("name", "");
        respuesta["objectId"] = nuevo;
    } else {
        const std::string ruta = entrada.value("path", "");
        const fs::path destino = fs::path(ProjectPaths::directorioSrc(proyecto)) / ruta;
        std::error_code ec;
        if (fs::exists(destino, ec) && !argumentos.value("overwrite", false)) {
            const std::string mensaje = "Ya existe " + ruta + " en src/: usa overwrite: true para reemplazarlo";
            registrarErrorUso("destination_exists", mensaje);
            return ResultadoComando::fallo("destination_exists", mensaje, codigosSalida::kErrorUso);
        }
        if (fs::exists(destino, ec)) fs::remove_all(destino, ec);
        if (!mover(carpetaPapelera(proyecto) / entrada.value("stored", ""), destino)) {
            registrarErrorOperacional("restore_failed", "No se pudo restaurar " + ruta);
            return ResultadoComando::fallo("restore_failed", "No se pudo restaurar " + ruta, codigosSalida::kFalloEscritura);
        }
        fs::remove_all(carpetaPapelera(proyecto) / std::to_string(id), ec);
        nlohmann::json restauradas = nlohmann::json::array(), sinRestaurar = nlohmann::json::array();
        restaurarReferencias(proyecto, ruta, entrada.value("references", nlohmann::json::array()), restauradas, sinRestaurar);
        respuesta["path"] = ruta;
        respuesta["referencesRestored"] = restauradas;
        respuesta["referencesNotRestored"] = sinRestaurar;
    }
    indice = leerIndice(proyecto);  // se relee: restaurar las referencias ejecutó otros comandos
    auto& e = indice["entradas"];
    e.erase(std::remove_if(e.begin(), e.end(), [&](const auto& x) { return x.value("id", 0) == id; }), e.end());
    escribirIndice(proyecto, indice);
    return ResultadoComando::exito(respuesta);
}

ResultadoComando papeleraVaciar(const nlohmann::json& argumentos) {
    std::string proyecto;
    ResultadoComando resultado;
    int id = -1;
    if (!resolverProyectoDesdeArgs(argumentos, proyecto, resultado) || !idDeArgs(argumentos, false, id, resultado)) {
        return resultado;
    }
    auto indice = leerIndice(proyecto);
    auto& entradas = indice["entradas"];
    nlohmann::json borradas = nlohmann::json::array();
    std::error_code ec;
    for (auto it = entradas.begin(); it != entradas.end();) {
        if (id >= 0 && it->value("id", 0) != id) {
            ++it;
            continue;
        }
        if (it->value("kind", "") == "objeto") {
            fs::remove(it->value("prefab", ""), ec);
            olvidarBorradoEnHistorial(proyecto, it->value("prefab", ""));
        } else {
            fs::remove_all(carpetaPapelera(proyecto) / std::to_string(it->value("id", 0)), ec);
        }
        borradas.push_back(it->value("id", 0));
        it = entradas.erase(it);
    }
    if (id >= 0 && borradas.empty()) {
        const std::string mensaje = "No hay nada con el id " + std::to_string(id) + " en la papelera";
        registrarErrorUso("not_in_trash", mensaje);
        return ResultadoComando::fallo("not_in_trash", mensaje, codigosSalida::kRecursoNoEncontrado);
    }
    escribirIndice(proyecto, indice);
    return ResultadoComando::exito({{"project", proyecto}, {"emptied", borradas.size()}, {"ids", borradas}});
}

}  // namespace funshi
