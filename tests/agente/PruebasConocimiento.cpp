/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: base de conocimiento (RAG) y herramientas de conocimiento.
#include "PruebasAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Conocimiento/BaseConocimiento.h"
#include "Agente/Harness.h"
#include "Agente/LLM/Herramientas/HerramientasConocimiento.h"
#include "Agente/LLM/Herramientas/PerfilesHerramientas.h"
#include "Nucleo/AyudaComandos.h"

#include <filesystem>
#include <fstream>

namespace pruebas_agente {

namespace con = funshi::agente::conocimiento;

namespace {

bool apareceEntre(const con::BaseConocimiento& base, const std::string& pedido, const std::string& id,
                  std::size_t k) {
    for (const auto& r : base.buscar(pedido, k)) {
        if (r.documento->id == id) return true;
    }
    return false;
}

}  // namespace

void probarConocimiento() {
    const con::BaseConocimiento base;
    CHECK(base.tamano() == funshi::ayudaComandos().size() + con::guiasIncluidas().size(),
          "la base contiene todos los comandos y guías");

    // Pedidos reales en español → el documento esperado entre los 3 primeros.
    const std::vector<std::pair<std::string, std::string>> casos = {
        {"mueve el objeto 6 a la posición 1,2,3", "scene.object.settransform"},
        {"rota el jugador 90 grados en Y", "scene.object.settransform"},
        {"agranda el cubo al doble de su escala", "scene.object.settransform"},
        {"agrega un script de movimiento al jugador", "guia.script"},
        {"dale física al cubo para que caiga con gravedad", "guia.fisica"},
        {"deshacer el último cambio de la escena", "scene.undo"},
        {"busca el objeto llamado Enemigo", "scene.object.findbyname"},
        {"lista los archivos de la carpeta Scripts", "project.src.list"},
        {"importar un modelo fbx al proyecto", "asset.import"},
        {"crea un objeto visible con un modelo 3d", "guia.objeto-con-modelo"},
        {"crea un proyecto nuevo 2d", "project.create"},
        {"cambia la variable velocidad del script", "scene.object.setfield"},
        {"duplica el objeto 4", "scene.object.clone"},
        {"qué errores hubo al compilar", "guia.errores"},
        {"muestra los últimos diagnósticos", "diagnostics.list"},
        {"haz que la lámpara sea hija de la mesa", "scene.object.setparent"},
        {"ponle la etiqueta Enemigo al objeto 3", "scene.object.settag"},
        {"agrégale un sonido de pasos", "guia.audio"},
        {"desactiva el objeto 5", "scene.object.setactive"},
    };
    for (const auto& [pedido, id] : casos) {
        std::string obtenidos;
        for (const auto& r : base.buscar(pedido, 3)) {
            obtenidos += " " + r.documento->id + "(" + std::to_string(r.puntaje).substr(0, 4) + ")";
        }
        CHECK(apareceEntre(base, pedido, id, 3), "RAG: \"" + pedido + "\" debería traer " + id + "; trajo" + obtenidos);
    }

    // Conversación sin pedido concreto: no se inyecta documentación.
    const auto saludo = base.buscar("hola, ¿cómo estás?", 1);
    CHECK(base.contextoParaPedido("hola, ¿cómo estás?", {}).empty(),
          "RAG: saludo sin contexto; mejor: " + (saludo.empty() ? std::string("-") : saludo[0].documento->id + " " + std::to_string(saludo[0].puntaje)));
    const std::string ctx = base.contextoParaPedido("mueve el objeto 6 a la posición 1,2,3", {});
    CHECK(ctx.find("scene.object.settransform") != std::string::npos, "RAG: contexto con settransform");
    CHECK(ctx.size() < 4000, "RAG: el contexto es compacto (" + std::to_string(ctx.size()) + " bytes)");

    // El ejemplo de la ayuda se convierte en la llamada JSON de ejecutar_comando.
    const auto* d = base.comando("scene.object.settransform");
    CHECK(d && d->contenido.find("\"position\":[0,1.5,0]") != std::string::npos, "doc: ejemplo con position");
    CHECK(d && d->contenido.find("\"project\":\"<proyecto>\"") != std::string::npos, "doc: ejemplo con project");
    CHECK(d && d->contenido.find("project (texto, obligatorio)") != std::string::npos, "doc: project obligatorio");

    // Documentos del usuario: cada encabezado ## es un documento.
    const auto dir = std::filesystem::current_path() / "conocimiento-prueba";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "juego.md") << "# Mi juego\nIntro.\n## Convención de enemigos\n"
                                       "Todos los enemigos llevan el tag Malo y viven bajo el objeto Horda.\n";
    const con::BaseConocimiento conUsuario(dir);
    CHECK(conUsuario.tamano() == base.tamano() + 2, "doc usuario: dos secciones");
    bool hallado = false;
    for (const auto& r : conUsuario.buscar("qué convención siguen los enemigos", 3)) {
        hallado = hallado || r.documento->titulo == "Convención de enemigos";
    }
    CHECK(hallado, "doc usuario: se recupera por su contenido");
}

void probarHerramientas() {
    using namespace funshi::agente::llm;
    funshi::Auditoria auditoria("auditoria-prueba.log");
    auto harness = std::make_shared<funshi::Harness>(auditoria);

    EjecutarComandoTool ejecutar(harness);
    auto r = ejecutar.execute({{"comando", "version"}}, {});
    CHECK(r.ok() && r.text.find("version") != std::string::npos, "ejecutar_comando: version");
    r = ejecutar.execute({{"comando", "project.src.delete"}, {"argumentos", {{"path", "x"}}}}, {});
    CHECK(!r.ok() && r.errorMessage.find("/project.src.delete") != std::string::npos,
          "ejecutar_comando: bloquea comandos destructivos");
    r = ejecutar.execute({{"comando", "scene.teletransportar"}}, {});
    CHECK(!r.ok() && r.errorMessage.find("buscar_comandos") != std::string::npos,
          "ejecutar_comando: comando desconocido sugiere buscar");
    r = ejecutar.execute({{"comando", "version"}, {"argumentos", "{}"}}, {});
    CHECK(r.ok(), "ejecutar_comando: acepta argumentos como texto JSON");

    r = ejecutar.execute({{"comando", "scene.list"}, {"argumentos", {{"project", "Proyecto Inventado"}}}}, {});
    CHECK(!r.ok() && r.errorMessage.find("nombre exacto de un proyecto existente") != std::string::npos,
          "ejecutar_comando: proyecto inexistente → lista los reales");

    BuscarComandosTool buscar({"scene.object.create"});
    r = buscar.execute({{"consulta", "scene.object.settransform"}}, {});
    CHECK(r.ok() && r.text.find("Sin herramienta propia") != std::string::npos,
          "buscar_comandos: nombre exacto → documentación completa");
    r = buscar.execute({{"consulta", "crear un objeto"}}, {});
    CHECK(r.ok() && r.text.find("Herramienta propia: scene.object.create") != std::string::npos,
          "buscar_comandos: indica la herramienta propia");

    // Perfiles: lectura no modifica; escena sí; nadie usa los bloqueados.
    CHECK(comandoPermitido("lectura", "scene.objects"), "perfil lectura: consulta");
    CHECK(!comandoPermitido("lectura", "scene.object.settransform"), "perfil lectura: no modifica");
    CHECK(comandoPermitido("escena", "scene.object.settransform"), "perfil escena: modifica");
    CHECK(!comandoPermitido("escena", "project.src.create"), "perfil escena: no toca archivos");
    CHECK(!comandoPermitido("completo", "project.src.delete"), "perfil completo: sin destructivos");
    auto registro = crearRegistroPerfil(harness, "lectura");
    auto via = registro->byName("ejecutar_comando");
    r = via->execute({{"comando", "scene.object.settransform"}, {"argumentos", {{"project", "X"}, {"id", 1}}}}, {});
    CHECK(!r.ok() && r.errorMessage.find("perfil 'lectura'") != std::string::npos,
          "perfil lectura: ejecutar_comando respeta el perfil");
    CHECK(!registro->byName("scene.object.create"), "perfil lectura: sin herramientas de escritura");
    CHECK(registro->byName("buscar_comandos") != nullptr, "perfil lectura: con buscar_comandos");
}

}  // namespace pruebas_agente
