/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Herramientas/EsquemasHerramientas.h"

#include "Nucleo/AyudaComandos.h"

#include <nlohmann/json.hpp>

namespace funshi::agente::llm {

namespace {

// Tipo JSON Schema del tipo de la ayuda ("entero", "[x,y,z]", "true | false"…);
// vacío si admite varios (texto | [r,g,b,a] | {...}).
std::string tipoJson(const std::string& tipo) {
    if (tipo.find('|') != std::string::npos && tipo != "true | false") return {};
    if (tipo == "texto") return "string";
    if (tipo == "entero") return "integer";
    if (tipo == "número" || tipo == "numero") return "number";
    if (tipo == "true | false") return "boolean";
    if (!tipo.empty() && tipo[0] == '[') return "array";
    if (!tipo.empty() && tipo[0] == '{') return "object";
    return {};
}

class ToolConAyuda : public Tool {
public:
    explicit ToolConAyuda(std::shared_ptr<Tool> base) : base_(std::move(base)), spec_(specDesdeAyuda(base_->spec())) {}
    ToolSpec spec() const override { return spec_; }
    ToolCategory category() const override { return base_->category(); }
    ToolOrigin origin() const override { return base_->origin(); }
    std::string describeCall(const nlohmann::json& args) const override { return base_->describeCall(args); }
    ToolResult execute(const nlohmann::json& args, const ToolContext& ctx) override { return base_->execute(args, ctx); }

private:
    std::shared_ptr<Tool> base_;
    ToolSpec spec_;
};

}  // namespace

ToolSpec specDesdeAyuda(const ToolSpec& original) {
    const AyudaComando* ayuda = buscarAyudaComando(original.name);
    if (!ayuda) return original;
    // ordered_json: el orden de las propiedades es el que ve el modelo.
    nlohmann::ordered_json props = nlohmann::ordered_json::object();
    nlohmann::ordered_json requeridos = nlohmann::ordered_json::array();
    if (ayuda->usaProyecto) {
        props["project"] = {{"type", "string"}, {"description", "Nombre del proyecto"}};
        requeridos.push_back("project");
    }
    for (const auto& arg : ayuda->argumentos) {
        nlohmann::ordered_json p = nlohmann::ordered_json::object();
        const std::string tipo = tipoJson(arg.tipo);
        if (!tipo.empty()) p["type"] = tipo;
        // El formato exacto (x,y,z; valores posibles) va en la descripción.
        const bool tipoInformativo = tipo.empty() || tipo == "array" || tipo == "object";
        p["description"] = arg.descripcion + (tipoInformativo ? " (" + arg.tipo + ")" : std::string());
        props[arg.nombre] = std::move(p);
        if (arg.requerido) requeridos.push_back(arg.nombre);
    }
    const auto previo = nlohmann::ordered_json::parse(original.parametersSchema, nullptr, false);
    if (previo.is_object() && previo.contains("properties") && previo["properties"].is_object()) {
        // Del esquema a mano solo se conserva lo que no es del comando (el
        // resumen de un archivo, que las herramientas quitan antes de ejecutar).
        if (previo["properties"].contains("resumen") && !props.contains("resumen")) {
            props["resumen"] = previo["properties"]["resumen"];
        }
    }
    ToolSpec s = original;
    s.description = ayuda->descripcion;
    nlohmann::ordered_json esquema = {{"type", "object"}, {"properties", props}};
    if (!requeridos.empty()) esquema["required"] = requeridos;
    s.parametersSchema = esquema.dump();
    return s;
}

std::shared_ptr<Tool> conEsquemaDeAyuda(std::shared_ptr<Tool> herramienta) {
    return std::make_shared<ToolConAyuda>(std::move(herramienta));
}

}  // namespace funshi::agente::llm
