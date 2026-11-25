/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Comandos/ComandosBorrado.h"

#include <Assets/AssetPath.h>
#include <Assets/Tipos/TiposArchivo.h>
#include <Configuracion/EditorConfig.h>
#include <Configuracion/ProjectPaths.h>
#include <Objetos/Prefab.h>
#include <Objetos/Componentes/Render/Material.h>
#include <Scenes/Editor/EditorController.h>

#include "Comandos/Escena/ComandosEscenaInterno.h"
#include "Comandos/ComandosPapelera.h"
#include "Escena/SerializacionEscena.h"
#include "Nucleo/Registro.h"
#include "Validacion/ValidacionArgs.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>

namespace funshi {

namespace fs = std::filesystem;

namespace {

// El constructor de Prefab crea <raíz de assets>/Prefabs. La CLI no fija esa
// raíz (quedaría en el directorio corriente): mientras dura la operación se
// fija en la papelera del proyecto.
class RaizAssetsTemporal {
public:
    explicit RaizAssetsTemporal(const fs::path& raiz) : anterior_(EditorConfig::raizAssetsFijada()) {
        EditorConfig::fijarRaizAssets(raiz.string());
    }
    ~RaizAssetsTemporal() {
        if (anterior_.empty()) EditorConfig::limpiarRaizAssets();
        else EditorConfig::fijarRaizAssets(anterior_);
    }

private:
    std::string anterior_;
};

std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Ids del objeto y de todos sus descendientes.
void idsDelSubarbol(GameObject* obj, nlohmann::json& ids) {
    ids.push_back(obj->getId());
    for (Entity* hijo : obj->getChildEntities()) {
        if (auto* go = dynamic_cast<GameObject*>(hijo)) idsDelSubarbol(go, ids);
    }
}

// Ruta de un archivo de src/ desde los argumentos: validada, sin escapar de src/.
bool archivoDeSrc(const nlohmann::json& argumentosIn, std::string& proyecto, std::string& rel, fs::path& abs,
                  ResultadoComando& resultado) {
    nlohmann::json argumentos = argumentosIn;
    if (!resolverProyectoDesdeArgs(argumentos, proyecto, resultado)) return false;
    std::string error;
    if (!validarArgumentos(argumentos, {{"path", true, nlohmann::json::value_t::string}}, error)) {
        registrarErrorUso("invalid_request", error);
        resultado = ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
        return false;
    }
    rel = AssetPath::normalize(argumentos["path"].get<std::string>());
    const fs::path src = ProjectPaths::directorioSrc(proyecto);
    abs = src / rel;
    if (!rutaSegura(src, abs.parent_path(), "La ruta intenta escapar del directorio src del proyecto", resultado)) {
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(abs, ec)) {
        const std::string mensaje = "No existe el archivo " + rel + " en src/";
        registrarErrorOperacional("file_not_found", mensaje);
        resultado = ResultadoComando::fallo("file_not_found", mensaje, codigosSalida::kRecursoNoEncontrado);
        return false;
    }
    return true;
}

// Quita las referencias al archivo en la escena, guarda si cambió y lo mueve a la papelera.
ResultadoComando borrarArchivoConReferencias(const std::string& proyecto, const std::string& rel, const fs::path& abs,
                                            const std::string& tipo) {
    nlohmann::json quitadas = nlohmann::json::array();
    {
        ContextoEscena contexto(proyecto);
        quitadas = quitarReferenciasA(&contexto.registro(), rel, abs.string());
        if (!quitadas.empty()) guardarEscena(contexto, proyecto);
    }
    std::string error;
    const int enPapelera = moverAPapelera(proyecto, rel, abs, tipo, quitadas, error);
    if (enPapelera < 0) {
        registrarErrorOperacional("delete_failed", error);
        return ResultadoComando::fallo("delete_failed", error, codigosSalida::kFalloEscritura);
    }
    return ResultadoComando::exito({{"project", proyecto},
                                    {"path", rel},
                                    {"type", tipo},
                                    {"deleted", true},
                                    {"trashId", enPapelera},
                                    {"referencesRemoved", quitadas}});
}

}  // namespace

nlohmann::json quitarReferenciasA(SceneRegistry* registro, const std::string& rutaRelativa,
                                  const std::string& rutaAbsoluta) {
    const std::string rel = minusculas(AssetPath::normalize(rutaRelativa));
    const std::string abs = minusculas(AssetPath::normalize(rutaAbsoluta));
    const auto apunta = [&](const std::string& valor) {
        const std::string v = minusculas(AssetPath::normalize(valor));
        if (v.empty()) return false;
        return v == rel || v == abs || (v.size() > rel.size() && v.compare(v.size() - rel.size() - 1, rel.size() + 1, "/" + rel) == 0);
    };
    std::function<bool(const nlohmann::json&)> referencia = [&](const nlohmann::json& j) {
        if (j.is_string()) return apunta(j.get<std::string>());
        if (j.is_object() || j.is_array()) {
            for (const auto& v : j) {
                if (referencia(v)) return true;
            }
        }
        return false;
    };
    nlohmann::json quitadas = nlohmann::json::array();
    std::function<void(GameObject*)> recorrer = [&](GameObject* obj) {
        std::vector<Component*> aQuitar;
        if (ListaDE<Component*>* comps = obj->getComponents(); comps && !comps->isEmpty()) {
            for (Position<Component*>* p = comps->first(); p; p = (p != comps->last()) ? comps->next(p) : nullptr) {
                Component* c = p->getElement();
                if (auto* material = dynamic_cast<Material*>(c)) {
                    // Un material conserva lo demás: solo se vacía el mapa que usaba el archivo.
                    const std::pair<const char*, std::function<void()>> mapas[] = {
                        {"diffuseMap", [material] { material->setDiffuseMapPath(""); }},
                        {"specularMap", [material] { material->setSpecularMapPath(""); }},
                        {"normalMap", [material] { material->setNormalMapPath(""); }},
                        {"emissionMap", [material] { material->setEmissionMapPath(""); }}};
                    const auto j = componenteJson(c);
                    for (const auto& [clave, vaciar] : mapas) {
                        if (j.contains(clave) && j[clave].is_string() && apunta(j[clave].get<std::string>())) {
                            vaciar();
                            quitadas.push_back({{"id", obj->getId()}, {"name", std::string(obj->inputName)},
                                                {"componentType", "Material"}, {"field", clave}});
                            obj->setDirty(true);
                        }
                    }
                    continue;
                }
                if (referencia(componenteJson(c))) aQuitar.push_back(c);
            }
        }
        for (Component* c : aQuitar) {
            quitadas.push_back({{"id", obj->getId()}, {"name", std::string(obj->inputName)}, {"componentType", tipoComponenteNombre(c)}});
            obj->deleteComponent(c);
            obj->setDirty(true);
        }
        for (Entity* hijo : obj->getChildEntities()) {
            if (auto* go = dynamic_cast<GameObject*>(hijo)) recorrer(go);
        }
    };
    if (GameObject* raiz = registro->getRoot()) recorrer(raiz);
    return quitadas;
}

ResultadoComando escenaObjetoBorrar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string proyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, proyecto, resultado)) return resultado;
    std::string error;
    if (!validarArgumentos(argumentos, {{"id", true, nlohmann::json::value_t::number_unsigned}}, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    const int id = static_cast<int>(argumentos["id"].get<unsigned>());
    if (id == 0) {
        registrarErrorUso("invalid_request", "La raíz de la escena no se puede borrar");
        return ResultadoComando::fallo("invalid_request", "La raíz de la escena (id 0) no se puede borrar",
                                       codigosSalida::kErrorUso);
    }
    ContextoEscena contexto(proyecto);
    SceneRegistry* registro = &contexto.registro();
    GameObject* obj = findObjectById(registro, id);
    if (!obj) {
        registrarErrorUso("object_not_found", "Objeto no encontrado");
        return ResultadoComando::fallo("object_not_found", "Objeto no encontrado", codigosSalida::kRecursoNoEncontrado);
    }
    const std::string nombre = std::string(obj->inputName);
    nlohmann::json ids = nlohmann::json::array();
    idsDelSubarbol(obj, ids);
    auto* padre = dynamic_cast<GameObject*>(obj->getParentEntity());
    const int padreId = padre ? padre->getId() : 0;

    // Copia para deshacer: el subárbol con sus componentes, como prefab.
    const fs::path papelera = carpetaPapelera(proyecto);
    const auto marca = std::chrono::system_clock::now().time_since_epoch().count();
    const fs::path copia = papelera / ("objeto-" + std::to_string(id) + "-" + std::to_string(marca) + ".prefab");
    bool conCopia = false;
    {
        RaizAssetsTemporal raiz(papelera);
        conCopia = Prefab::desdeArchivo(copia.string())->guardarDesdeObjeto(obj, &contexto.editor());
    }
    if (!borrarConDescendientes(contexto, obj)) {
        registrarErrorOperacional("delete_failed", "No se pudo borrar el objeto id=" + std::to_string(id));
        return ResultadoComando::fallo("delete_failed", "No se pudo borrar el objeto", codigosSalida::kFalloGeneral);
    }
    int enPapelera = -1;
    if (conCopia) {
        registrarOperacionUndo(proyecto, "delete", id,
                               nlohmann::json{{"prefab", copia.string()}, {"parentId", padreId}, {"name", nombre}},
                               nlohmann::json::object());
        enPapelera = anotarObjetoEnPapelera(proyecto, nombre, id, padreId, copia.string());
    }
    guardarEscena(contexto, proyecto);
    nlohmann::json respuesta = {{"project", proyecto}, {"id", id},          {"name", nombre},
                                {"deleted", true},     {"deletedIds", ids}, {"undoable", conCopia}};
    if (enPapelera >= 0) respuesta["trashId"] = enPapelera;
    return ResultadoComando::exito(respuesta);
}

// De las hojas hacia arriba: SceneRegistry::deleteObject solo saca el nodo
// del árbol, y los descendientes que quedan en el registro reaparecerían
// colgados de la raíz al guardar.
bool borrarConDescendientes(ContextoEscena& contexto, GameObject* objeto) {
    nlohmann::json ids = nlohmann::json::array();
    idsDelSubarbol(objeto, ids);
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        const int actual = it->get<int>();
        if (!contexto.editor().deleteObjectByID(actual) && !contexto.registro().deleteObjectByID(actual)) return false;
    }
    return true;
}

int restaurarObjetoBorrado(ContextoEscena& contexto, const nlohmann::json& antes) {
    const std::string ruta = antes.value("prefab", std::string());
    std::error_code ec;
    if (ruta.empty() || !fs::is_regular_file(ruta, ec)) return -1;
    SceneRegistry* registro = &contexto.registro();
    GameObject* padre = findObjectById(registro, antes.value("parentId", 0));
    if (!padre) padre = registro->getRoot();
    RaizAssetsTemporal raiz(fs::path(ruta).parent_path());
    GameObject* restaurado = Prefab::desdeArchivo(ruta)->instanciar(&contexto.editor(), padre);
    if (!restaurado) return -1;
    restaurado->setDirty(true);
    return restaurado->getId();
}

ResultadoComando scriptBorrar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string proyecto, rel;
    fs::path abs;
    ResultadoComando resultado;
    if (!archivoDeSrc(argumentos, proyecto, rel, abs, resultado)) return resultado;
    const TipoArchivo tipo = TiposArchivo::clasificar(rel);
    if (tipo != TipoArchivo::ScriptJava && tipo != TipoArchivo::ScriptCpp) {
        const std::string mensaje = rel + " no es un script (.java o .cpp): para un asset usa asset.delete";
        registrarErrorUso("not_a_script", mensaje);
        return ResultadoComando::fallo("not_a_script", mensaje, codigosSalida::kErrorUso);
    }
    return borrarArchivoConReferencias(proyecto, rel, abs, TiposArchivo::nombre(tipo));
}

ResultadoComando assetBorrar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string proyecto, rel;
    fs::path abs;
    ResultadoComando resultado;
    if (!archivoDeSrc(argumentos, proyecto, rel, abs, resultado)) return resultado;
    const TipoArchivo tipo = TiposArchivo::clasificar(abs.string());
    if (tipo == TipoArchivo::ScriptJava || tipo == TipoArchivo::ScriptCpp) {
        const std::string mensaje = rel + " es un script: usa script.delete";
        registrarErrorUso("not_an_asset", mensaje);
        return ResultadoComando::fallo("not_an_asset", mensaje, codigosSalida::kErrorUso);
    }
    if (tipo == TipoArchivo::Desconocido || tipo == TipoArchivo::Proyecto) {
        const std::string mensaje = rel + " no es un asset del motor (modelo, imagen, audio, prefab, tileset, "
                                          "perfil de partículas o interfaz): para otro archivo usa project.src.delete";
        registrarErrorUso("not_an_asset", mensaje);
        return ResultadoComando::fallo("not_an_asset", mensaje, codigosSalida::kErrorUso);
    }
    return borrarArchivoConReferencias(proyecto, rel, abs, TiposArchivo::nombre(tipo));
}

ResultadoComando assetListar(const nlohmann::json& argumentosIn) {
    nlohmann::json argumentos = argumentosIn;
    std::string proyecto;
    ResultadoComando resultado;
    if (!resolverProyectoDesdeArgs(argumentos, proyecto, resultado)) return resultado;
    std::string error;
    if (!validarArgumentos(argumentos, {{"type", false, nlohmann::json::value_t::string}}, error)) {
        registrarErrorUso("invalid_request", error);
        return ResultadoComando::fallo("invalid_request", error, codigosSalida::kErrorUso);
    }
    const std::string filtro = minusculas(argumentos.value("type", std::string()));
    const fs::path src = ProjectPaths::directorioSrc(proyecto);
    nlohmann::json assets = nlohmann::json::array();
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(src, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const TipoArchivo tipo = TiposArchivo::clasificar(it->path().string());
        if (tipo == TipoArchivo::Desconocido || tipo == TipoArchivo::Proyecto || tipo == TipoArchivo::ScriptJava ||
            tipo == TipoArchivo::ScriptCpp) {
            continue;
        }
        const std::string nombreTipo = TiposArchivo::nombre(tipo);
        if (!filtro.empty() && minusculas(nombreTipo).find(filtro) == std::string::npos) continue;
        assets.push_back({{"path", AssetPath::normalize(fs::relative(it->path(), src, ec).generic_string())},
                          {"type", nombreTipo},
                          {"size", static_cast<std::uint64_t>(it->file_size(ec))}});
    }
    std::sort(assets.begin(), assets.end(), [](const auto& a, const auto& b) { return a["path"] < b["path"]; });
    return ResultadoComando::exito({{"project", proyecto}, {"count", assets.size()}, {"assets", assets}});
}

}  // namespace funshi
