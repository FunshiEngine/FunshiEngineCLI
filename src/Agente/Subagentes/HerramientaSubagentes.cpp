/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Subagentes/HerramientaSubagentes.h"

#include "Agente/LLM/Herramientas/PerfilesHerramientas.h"

namespace funshi::agente {

llm::ToolSpec LanzarSubagentesTool::spec() const {
    return llm::ToolSpec{
        "lanzar_subagentes",
        "Delega 1 a 4 tareas INDEPENDIENTES en subagentes; cada uno trabaja con su propio "
        "contexto y devuelve un informe. Úsala para pedidos con varias partes independientes "
        "que requieren varias herramientas cada una, o para revisar mucha información y "
        "recibir solo el resumen. No la uses para una sola acción ni para pasos que dependen "
        "entre sí. El subagente NO ve la conversación: pon en contexto todos los datos. "
        "Ejemplo de argumentos: {\"tareas\":[{\"titulo\":\"Crear Enemigo1\",\"instrucciones\":"
        "\"Crea el objeto Enemigo1 y agrégale un Collider esfera de radio 0.5; informa su id\","
        "\"contexto\":\"project=Prueba\",\"perfil\":\"escena\"},{\"titulo\":\"Crear Enemigo2\","
        "\"instrucciones\":\"Crea el objeto Enemigo2 y agrégale un RigidBody de masa 2; informa su id\","
        "\"contexto\":\"project=Prueba\",\"perfil\":\"escena\"}]}",
        R"J({"type":"object","properties":{"tareas":{"type":"array","minItems":1,"maxItems":4,)J"
        R"J("items":{"type":"object","properties":{)J"
        R"J("titulo":{"type":"string","description":"Qué se hace, corto"},)J"
        R"J("instrucciones":{"type":"string","description":"Objetivo concreto, cuándo está terminada y qué informar"},)J"
        R"J("contexto":{"type":"string","description":"Datos necesarios: proyecto, nombres, ids, rutas, valores"},)J"
        R"J("perfil":{"type":"string","enum":["lectura","escena","archivos","scripts","completo"],)J"
        R"J("description":"Herramientas del subagente: el perfil más chico que alcance"}},)J"
        R"J("required":["titulo","instrucciones","contexto","perfil"]}}},"required":["tareas"]})J"};
}

bool LanzarSubagentesTool::leerTareas(const nlohmann::json& args, std::vector<TareaSubagente>& tareas,
                                      std::string& error) {
    nlohmann::json lista = args.contains("tareas") ? args["tareas"] : nlohmann::json();
    if (lista.is_string()) lista = nlohmann::json::parse(lista.get<std::string>(), nullptr, false);
    if (!lista.is_array() || lista.empty()) {
        error = "'tareas' debe ser una lista con 1 a 4 tareas";
        return false;
    }
    if (lista.size() > GestorSubagentes::kMaxTareas) {
        error = "Máximo 4 tareas por llamada: agrupa o lanza otra tanda después";
        return false;
    }
    for (std::size_t i = 0; i < lista.size(); ++i) {
        const auto& t = lista[i];
        const std::string n = "tarea " + std::to_string(i + 1);
        if (!t.is_object()) {
            error = n + ": debe ser un objeto {titulo, instrucciones, contexto, perfil}";
            return false;
        }
        TareaSubagente tarea;
        tarea.titulo = t.value("titulo", std::string());
        tarea.instrucciones = t.value("instrucciones", std::string());
        tarea.contexto = t.value("contexto", std::string());
        tarea.perfil = t.value("perfil", std::string("completo"));
        if (tarea.titulo.empty()) tarea.titulo = "Tarea " + std::to_string(i + 1);
        if (tarea.instrucciones.size() < 15) {
            error = n + ": las instrucciones son demasiado vagas; di el objetivo concreto, "
                        "cuándo está terminada y qué informar";
            return false;
        }
        if (!llm::perfilValido(tarea.perfil)) {
            // Perfil mal escrito ("lectura/escena", "edición"): se usa el completo,
            // que no da más permisos que los del agente principal.
            tarea.perfil = "completo";
        }
        tareas.push_back(std::move(tarea));
    }
    return true;
}

llm::ToolResult LanzarSubagentesTool::execute(const nlohmann::json& args, const llm::ToolContext& ctx) {
    std::vector<TareaSubagente> tareas;
    std::string error;
    if (tandas_ >= kMaxTandas) {
        return llm::ToolResult::error(
            "Ya delegaste " + std::to_string(kMaxTandas) +
            " tandas en este pedido. No vuelvas a delegar: termina tú con las herramientas o "
            "responde al usuario con lo que informaron los subagentes.");
    }
    if (!leerTareas(args, tareas, error)) return llm::ToolResult::error(error);
    ++tandas_;
    const auto informes = gestor_->ejecutar(tareas, ctx.onEvent);
    return llm::ToolResult::ok(GestorSubagentes::resumen(informes));
}

}  // namespace funshi::agente
