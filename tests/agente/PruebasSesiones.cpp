/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: sesiones de chat (persistencia y procesamiento en paralelo).
#include "PruebasAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/Sesiones/SesionChat.h"

#include <filesystem>
#include <chrono>
#include <fstream>
#include <thread>

namespace pruebas_agente {

using funshi::agente::AlmacenSesiones;
using funshi::agente::SesionChat;

void probarSesiones() {
    const auto dir = std::filesystem::current_path() / "sesiones-prueba";
    const AlmacenSesiones almacen(dir);

    SesionChat a = SesionChat::nueva("Mover objetos");
    a.actualizada = 100;
    a.agente.proyectoActivo = "MiJuego";
    a.mensajes.push_back({"usuario", "mueve el cubo"});
    a.agente.historial.push_back(llm::ChatMessage::system("sistema"));
    a.agente.historial.push_back(llm::ChatMessage::user("mueve el cubo"));
    a.agente.historial.push_back(llm::ChatMessage::assistantWithTools(
        "", nlohmann::json::array({{{"id", "c1"}, {"type", "function"},
                                    {"function", {{"name", "ejecutar_comando"}, {"arguments", "{}"}}}}})));
    a.agente.historial.push_back(llm::ChatMessage::tool("c1", "{\"ok\":true}", "ejecutar_comando"));
    SesionChat b = SesionChat::nueva("Otra");
    b.actualizada = 200;
    CHECK(a.id != b.id, "sesiones: ids distintos");
    CHECK(almacen.guardar(a) && almacen.guardar(b), "sesiones: se guardan");

    const auto todas = almacen.cargarTodas();
    CHECK(todas.size() == 2 && todas[0].id == b.id, "sesiones: la más reciente primero");
    const SesionChat& leida = todas.size() == 2 ? todas[1] : a;
    CHECK(leida.titulo == "Mover objetos" && leida.agente.proyectoActivo == "MiJuego", "sesiones: título y proyecto");
    CHECK(leida.mensajes.size() == 1 && leida.mensajes[0].texto == "mueve el cubo", "sesiones: mensajes visibles");
    CHECK(leida.agente.historial.size() == 4 && leida.agente.historial[2].toolCalls &&
              leida.agente.historial[3].toolCallId == std::optional<std::string>("c1"),
          "sesiones: historial con tool calls y resultados");

    // Recorte del historial: se conserva el mensaje de sistema.
    SesionChat larga = SesionChat::nueva("Larga");
    larga.agente.historial.push_back(llm::ChatMessage::system("sistema"));
    for (int i = 0; i < 300; ++i) larga.agente.historial.push_back(llm::ChatMessage::user(std::to_string(i)));
    almacen.guardar(larga);
    for (const auto& s : almacen.cargarTodas()) {
        if (s.id != larga.id) continue;
        CHECK(s.agente.historial.size() == AlmacenSesiones::kMaxHistorial &&
                  s.agente.historial.front().role == "system" && s.agente.historial.back().content == "299",
              "sesiones: historial recortado conservando sistema y lo último");
    }
    CHECK(almacen.borrar(b.id) && almacen.cargarTodas().size() == 2, "sesiones: se borran");
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "rota.json") << "{no es json";
    CHECK(almacen.cargarTodas().size() == 2, "sesiones: ignora archivos dañados");

    CHECK(SesionChat::tituloDesde("  mueve el cubo  ") == "mueve el cubo", "título: corto");
    const std::string t = SesionChat::tituloDesde("crea tres enemigos con física y un script distinto cada uno por favor");
    CHECK(t.size() <= 44 && t.rfind("crea tres enemigos", 0) == 0, "título: recortado en palabra (" + t + ")");

    // Dos sesiones procesando a la vez con el mismo Silvy: historiales separados.
    funshi::Auditoria auditoria("auditoria-sesiones.log");
    funshi::Harness harness(auditoria);
    funshi::Silvy silvy(harness);
    auto registro = std::make_shared<GatewayGuion::Registro>();
    silvy.configurarConFabrica(
        [registro]() {
            return std::make_shared<GatewayGuion>(
                [](const LlamadaVista& v) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText("eco: " + v.mensajes.back().content.substr(
                        v.mensajes.back().content.rfind('\n') + 1))};
                },
                registro);
        },
        true, 1);
    funshi::SesionAgente s1, s2;
    funshi::Silvy::RespuestaProcesada r1, r2;
    std::thread h1([&]() { r1 = silvy.procesar("hola uno", s1, nullptr); });
    std::thread h2([&]() { r2 = silvy.procesar("hola dos", s2, nullptr); });
    h1.join();
    h2.join();
    CHECK(r1.texto == "eco: hola uno" && r2.texto == "eco: hola dos", "paralelo: cada sesión su respuesta");
    CHECK(s1.historial.size() == 3 && s2.historial.size() == 3, "paralelo: historiales separados (sistema, pedido, respuesta)");
}

}  // namespace pruebas_agente
