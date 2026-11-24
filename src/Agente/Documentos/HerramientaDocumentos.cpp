/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Documentos/HerramientaDocumentos.h"

#include "Agente/Documentos/BaseDocumentos.h"

#include <algorithm>

namespace funshi::agente::documentos {

llm::ToolSpec BuscarDocumentosTool::spec() const {
    return {"buscar_documentos",
            "Busca en los documentos que el usuario guardó para ti (PDF, Word, texto, código…). "
            "Úsala cuando el pedido trate de información de esos documentos (reglas del juego, "
            "diseño, especificaciones). Devuelve los fragmentos más relacionados con su archivo y página.",
            R"J({"type":"object","properties":{"consulta":{"type":"string","description":"Qué buscar, en una frase"},"max":{"type":"integer","description":"Cantidad de fragmentos (1 a 8, por defecto 4)"}},"required":["consulta"]})J"};
}

llm::ToolResult BuscarDocumentosTool::execute(const nlohmann::json& args, const llm::ToolContext&) {
    const std::string consulta = args.value("consulta", std::string());
    if (consulta.empty()) return llm::ToolResult::error("Falta \"consulta\".");
    const std::size_t max = static_cast<std::size_t>(std::clamp(args.value("max", 4), 1, 8));
    auto& base = BaseDocumentos::global();
    const auto estado = base.estado();
    if (estado.archivos == 0) {
        return llm::ToolResult::ok("No hay documentos indexados. El usuario puede dejarlos en " +
                                   base.carpeta().string() + " y escribir /docs actualizar.");
    }
    const auto proveedor = ServidorEmbeddings::global().activo();
    const auto resultados = base.buscar(consulta, max, proveedor.get());
    if (resultados.empty()) return llm::ToolResult::ok("Ningún fragmento de los documentos coincide con la consulta.");
    nlohmann::json lista = nlohmann::json::array();
    for (const auto& r : resultados) {
        nlohmann::json item = {{"archivo", r.ruta}, {"puntaje", static_cast<int>(r.puntaje * 100) / 100.0}, {"texto", r.texto}};
        if (r.pagina > 0) item["pagina"] = r.pagina;
        lista.push_back(std::move(item));
    }
    return llm::ToolResult::ok(nlohmann::json{{"busqueda", resultados.front().vectorial ? "semántica" : "por palabras"},
                                              {"fragmentos", lista}}.dump());
}

}  // namespace funshi::agente::documentos
