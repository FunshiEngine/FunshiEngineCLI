/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Herramientas/HerramientasConocimiento.h"

#include "Agente/Conocimiento/BaseConocimiento.h"
#include "Nucleo/CatalogoComandos.h"

#include <algorithm>

namespace funshi::agente::llm {

ToolSpec BuscarComandosTool::spec() const {
    return ToolSpec{
        "buscar_comandos",
        "Busca en la documentación de los comandos del motor y en las guías paso a paso. "
        "Úsala cuando no sepas qué comando usar o con qué argumentos. Con el nombre exacto "
        "de un comando (ej. scene.object.settransform) devuelve su documentación completa.",
        R"J({"type":"object","properties":{)J"
        R"J("consulta":{"type":"string","description":"Qué quieres hacer, en palabras, o el nombre de un comando"},)J"
        R"J("max":{"type":"integer","description":"Máximo de resultados (1-8, por defecto 4)"}},)J"
        R"J("required":["consulta"]})J"};
}

ToolResult BuscarComandosTool::execute(const nlohmann::json& args, const ToolContext&) {
    const std::string consulta = args.value("consulta", std::string());
    if (consulta.empty()) return ToolResult::error("Falta 'consulta'");
    const auto base = conocimiento::BaseConocimiento::global();
    if (const auto* d = base->comando(consulta)) {
        return ToolResult::ok(conocimiento::BaseConocimiento::formatear(*d, herramientas_));
    }
    const int max = std::clamp(args.value("max", 4), 1, 8);
    const auto resultados = base->buscar(consulta, static_cast<std::size_t>(max));
    if (resultados.empty()) {
        return ToolResult::ok("Sin resultados para \"" + consulta +
                              "\". Prueba con otras palabras (ej. el objeto y la acción: "
                              "\"mover objeto\", \"agregar script\").");
    }
    std::string out;
    for (const auto& r : resultados) {
        out += conocimiento::BaseConocimiento::formatear(*r.documento, herramientas_) + "\n\n";
    }
    return ToolResult::ok(out);
}

ToolSpec EjecutarComandoTool::spec() const {
    return ToolSpec{
        "ejecutar_comando",
        "Ejecuta cualquier comando del motor por su nombre exacto (ej. scene.object.settransform, "
        "scene.object.addcomponent, project.src.list). Úsala para los comandos que no tienen "
        "herramienta propia. Los argumentos son los de la documentación (buscar_comandos).",
        R"J({"type":"object","properties":{)J"
        R"J("comando":{"type":"string","description":"Nombre exacto del comando"},)J"
        R"J("argumentos":{"type":"object","description":"Argumentos del comando, incluido project si lo requiere"}},)J"
        R"J("required":["comando"]})J"};
}

const std::set<std::string>& EjecutarComandoTool::bloqueados() {
    static const std::set<std::string> b = {
        "diagnostics.clear",   // borra el historial de diagnósticos
        "cli.config.set",      // cambia rutas y el modelo de la propia CLI
        "agente.procesar",     // recursión del agente
    };
    return b;
}

const std::set<std::string>& EjecutarComandoTool::requierenConfirmacion() {
    static const std::set<std::string> c = {"scene.object.delete", "scene.objects.delete", "script.delete", "asset.delete",
                                            "project.src.delete", "trash.empty"};
    return c;
}

std::string EjecutarComandoTool::descripcionDeBorrado(const std::string& comando, const nlohmann::json& argumentos) {
    const std::string proyecto = argumentos.value("project", harness_ ? harness_->proyectoPorDefecto() : std::string());
    const std::string enProyecto = proyecto.empty() ? std::string() : " del proyecto " + proyecto;
    const std::string ruta = argumentos.value("path", std::string());
    if (comando == "scene.object.delete") {
        const auto id = argumentos.contains("id") ? argumentos["id"].dump() : std::string("?");
        std::string nombre;
        if (harness_ && argumentos.contains("id")) {
            funshi::Solicitud ver;
            ver.comando = "scene.object.get";
            ver.argumentos = {{"id", argumentos["id"]}};
            if (!proyecto.empty()) ver.argumentos["project"] = proyecto;
            const auto r = harness_->ejecutar(ver);
            if (r.respuesta.correcta) nombre = r.respuesta.resultado.value("object", nlohmann::json::object()).value("name", "");
        }
        return "borrar el objeto " + (nombre.empty() ? "" : "«" + nombre + "» ") + "(id " + id + ") y sus hijos de la escena" +
               enProyecto;
    }
    if (comando == "scene.objects.delete") {
        if (argumentos.value("all", false)) return "borrar TODOS los objetos de la escena" + enProyecto + " (la raíz se conserva)";
        if (argumentos.contains("ids") && argumentos["ids"].is_array()) {
            return "borrar " + std::to_string(argumentos["ids"].size()) + " objeto(s) de la escena" + enProyecto + " y sus hijos";
        }
        return "borrar objetos de la escena" + enProyecto;
    }
    if (comando == "trash.empty") {
        return argumentos.contains("id") ? "borrar para siempre la entrada " + argumentos["id"].dump() + " de la papelera" + enProyecto
                                         : "vaciar la papelera" + enProyecto + " (lo borrado ya no se podrá recuperar)";
    }
    if (comando == "script.delete") return "borrar el script " + ruta + enProyecto + " y quitarlo de los objetos que lo usan";
    if (comando == "asset.delete") return "borrar el asset " + ruta + enProyecto + " y quitarlo de los objetos que lo usan";
    return "borrar " + ruta + " de src/" + enProyecto;
}

ToolResult EjecutarComandoTool::execute(const nlohmann::json& args, const ToolContext& ctx) {
    const std::string comando = args.value("comando", std::string());
    if (comando.empty()) return ToolResult::error("Falta 'comando'");
    if (comandoDesdeTexto(comando) == ComandoCLI::Desconocido) {
        return ToolResult::error("Comando desconocido: " + comando +
                                 ". Usa buscar_comandos para encontrar el nombre correcto.");
    }
    if (bloqueados().count(comando)) {
        return ToolResult::error("El agente no puede ejecutar " + comando +
                                 " por su cuenta. Pídele al usuario que lo ejecute con /" + comando + ".");
    }
    // "argumentos" es la clave pedida; los modelos también usan las inglesas.
    nlohmann::json argumentos = nlohmann::json::object();
    for (const char* clave : {"argumentos", "arguments", "args", "parameters", "parametros"}) {
        if (args.contains(clave)) {
            argumentos = args[clave];
            break;
        }
    }
    if (argumentos.is_string()) {
        // Algunos modelos mandan el objeto como texto JSON.
        argumentos = nlohmann::json::parse(argumentos.get<std::string>(), nullptr, false);
    }
    if (!argumentos.is_object()) return ToolResult::error("'argumentos' debe ser un objeto JSON");
    if (requierenConfirmacion().count(comando)) {
        PermissionRequest pedido;
        pedido.chatId = ctx.chatId;
        pedido.tool = comando;
        pedido.category = ToolCategory::FILES;
        pedido.summary = descripcionDeBorrado(comando, argumentos);
        pedido.argsJson = argumentos.dump();
        if (!ctx.requestPermission || ctx.requestPermission(pedido) == PermissionDecision::DENY) {
            return ToolResult::error("El usuario no autorizó " + pedido.summary +
                                     ". No lo vuelvas a intentar salvo que el usuario lo pida de nuevo.");
        }
    }

    ToolResult r = executeCommand(comando, argumentos);
    if (r.ok() && r.text.size() > kMaxResultado) {
        r.text = r.text.substr(0, kMaxResultado) +
                 "…[resultado recortado: usa un comando más específico para ver el resto]";
    }
    return r;
}

}  // namespace funshi::agente::llm
