/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Herramientas/PerfilesHerramientas.h"

#include "Agente/LLM/Herramientas/EngineToolFactory.h"
#include "Agente/LLM/Herramientas/HerramientasConocimiento.h"

#include <set>

namespace funshi::agente::llm {

namespace {

const std::set<std::string>& comandosLectura() {
    static const std::set<std::string> s = {
        "help", "version", "project.list", "project.config.get", "project.src.list",
        "scene.list", "scene.objects", "scene.object.get", "scene.object.getglobalpos",
        "scene.hierarchy", "scene.object.components", "scene.object.findbytag",
        "scene.object.findbyname", "scene.object.getglobaltransform", "asset.validar",
        "asset.extensions", "script.list", "diagnostics.list", "cli.config.get",
    };
    return s;
}

const std::set<std::string>& comandosEscena() {
    static const std::set<std::string> s = {
        "scene.object.settransform", "scene.object.setcomponent", "scene.save",
        "scene.object.setparent", "scene.object.addcomponent", "scene.object.create",
        "scene.object.clone", "scene.object.removecomponent", "scene.object.unparent",
        "scene.object.settag", "scene.object.setactive", "scene.undo", "scene.redo",
        "scene.autosave",
    };
    return s;
}

const std::set<std::string>& comandosArchivos() {
    static const std::set<std::string> s = {
        "project.src.create", "project.src.rename", "project.dir.import", "project.fs.create",
        "file.create", "asset.import",
    };
    return s;
}

const std::set<std::string>& comandosScripts() {
    static const std::set<std::string> s = {
        "project.src.create", "script.compile", "scene.script.compile", "scene.script.reload",
        "scene.object.setfield", "scene.object.addcomponent",
    };
    return s;
}

}  // namespace

const std::vector<PerfilHerramientas>& perfilesHerramientas() {
    static const std::vector<PerfilHerramientas> p = {
        {"lectura", "solo consultar (listar, obtener, buscar); no modifica nada"},
        {"escena", "consultar y modificar la escena: objetos, transform, componentes, jerarquía"},
        {"archivos", "consultar y manejar archivos de src/, el vfs fs/ e importar assets"},
        {"scripts", "consultar, crear scripts en src/, compilar, recargar y cambiar campos"},
        {"completo", "todos los comandos permitidos al agente"},
    };
    return p;
}

bool perfilValido(const std::string& perfil) {
    for (const auto& p : perfilesHerramientas()) {
        if (perfil == p.nombre) return true;
    }
    return false;
}

bool comandoPermitido(const std::string& perfil, const std::string& comando) {
    if (EjecutarComandoTool::bloqueados().count(comando)) return false;
    if (perfil == "completo") return true;
    if (comandosLectura().count(comando)) return true;
    if (perfil == "escena") return comandosEscena().count(comando) > 0;
    if (perfil == "archivos") return comandosArchivos().count(comando) > 0;
    if (perfil == "scripts") return comandosScripts().count(comando) > 0;
    return false;  // lectura
}

namespace {

// ejecutar_comando limitado a los comandos del perfil.
class EjecutarComandoPerfilTool : public EjecutarComandoTool {
public:
    EjecutarComandoPerfilTool(std::shared_ptr<Harness> harness, std::string perfil)
        : EjecutarComandoTool(std::move(harness)), perfil_(std::move(perfil)) {}
    ToolResult execute(const nlohmann::json& args, const ToolContext& ctx) override {
        const std::string comando = args.value("comando", std::string());
        if (!comando.empty() && !EjecutarComandoTool::bloqueados().count(comando) &&
            !comandoPermitido(perfil_, comando)) {
            return ToolResult::error("El perfil '" + perfil_ + "' no permite " + comando + ".");
        }
        return EjecutarComandoTool::execute(args, ctx);
    }

private:
    std::string perfil_;
};

}  // namespace

std::shared_ptr<ToolRegistry> crearRegistroPerfil(std::shared_ptr<Harness> harness,
                                                  const std::string& perfil) {
    auto completo = createEngineToolRegistry(harness);
    auto registro = std::make_shared<ToolRegistry>();
    std::set<std::string> nombres;
    for (const auto& tool : completo->all()) {
        const std::string nombre = tool->spec().name;
        if (!comandoPermitido(perfil, nombre)) continue;
        registro->add(tool);
        nombres.insert(nombre);
    }
    registro->add(std::make_shared<BuscarComandosTool>(nombres));
    registro->add(std::make_shared<EjecutarComandoPerfilTool>(harness, perfil));
    return registro;
}

}  // namespace funshi::agente::llm
