/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
// agente-tests: túneles por sesión — reserva de slots, reemplazo del menos
// usado con vaciado, y en Silvy: cada sesión por su túnel y sin nada de la
// conversación de otra en lo que recibe el modelo.
#include "PruebasAgente.h"

#include "Agente/Auditoria.h"
#include "Agente/Harness.h"
#include "Agente/LLM/Cliente/AsignadorTuneles.h"
#include "Agente/Silvy.h"

#include <algorithm>

namespace pruebas_agente {

void probarTuneles() {
    using llm::AsignadorTuneles;
    std::vector<int> vaciados;
    auto dos = std::make_shared<AsignadorTuneles>([] { return 2; }, [&](int s) { vaciados.push_back(s); return true; });
    {
        auto a = dos->reservar("A");
        auto b = dos->reservar("B");
        CHECK(a.slot() == 0 && b.slot() == 1 && vaciados.empty(), "túneles: cada sesión su slot libre");
        auto otraVezA = dos->reservar("A");
        CHECK(otraVezA.slot() == 0, "túneles: la misma sesión reusa su slot (subagentes del pedido)");
        auto c = dos->reservar("C");
        CHECK(c.slot() == -1 && vaciados.empty(), "túneles: con todos reservados no se pisa ninguno");
    }
    {
        // A usó su slot más recientemente que B: el de B es el menos usado.
        auto c = dos->reservar("C");
        CHECK(c.slot() == 1 && vaciados == std::vector<int>{1}, "túneles: el menos usado cambia de dueño y se vacía");
        auto a = dos->reservar("A");
        CHECK(a.slot() == 0 && vaciados.size() == 1, "túneles: el dueño vuelve a su slot sin vaciar");
    }
    auto sinSlots = std::make_shared<AsignadorTuneles>([] { return 0; }, [](int) { return true; });
    CHECK(sinSlots->reservar("A").slot() == -1, "túneles: servidor sin slots → sin túnel");
    std::vector<int> vaciadosUno;
    auto uno = std::make_shared<AsignadorTuneles>([] { return 1; }, [&](int s) { vaciadosUno.push_back(s); return true; });
    { auto a = uno->reservar("A"); }
    { auto b = uno->reservar("B"); }
    CHECK(vaciadosUno == std::vector<int>{0}, "túneles: con un solo slot, cambiar de sesión vacía la caché");

    // Silvy: dos sesiones, cada una por su túnel y con su propia conversación.
    funshi::Auditoria auditoria("auditoria-tuneles.log");
    funshi::Harness harness(auditoria);
    funshi::Silvy silvy(harness);
    auto registro = std::make_shared<GatewayGuion::Registro>();
    silvy.configurarConFabrica(
        [registro]() {
            return std::make_shared<GatewayGuion>(
                [](const LlamadaVista&) { return std::vector<llm::LlmChunk>{llm::LlmChunk::makeText("Listo.")}; }, registro);
        },
        true, 1);
    silvy.setTuneles(std::make_shared<AsignadorTuneles>([] { return 4; }, [](int) { return true; }));
    silvy.setTareasActivas(true);
    funshi::SesionAgente s1, s2;
    s1.id = "sesion-1";
    s2.id = "sesion-2";
    silvy.procesar("crea un dragón rojo", s1, nullptr);
    silvy.procesar("¿qué objetos hay?", s2, nullptr);
    silvy.procesar("ponle alas", s1, nullptr);
    std::vector<int> tunelesS1, tunelesS2;
    bool fuga = false;
    {
        std::lock_guard<std::mutex> lock(registro->mutex);
        for (const auto& l : registro->llamadas) {
            std::string todo;
            for (const auto& m : l.mensajes) todo += m.content + "\n";
            const bool esS2 = todo.find("¿qué objetos hay?") != std::string::npos;
            (esS2 ? tunelesS2 : tunelesS1).push_back(l.tunel);
            if (esS2 && todo.find("dragón") != std::string::npos) fuga = true;
        }
    }
    const auto todos = [](const std::vector<int>& v, int x) {
        return !v.empty() && std::all_of(v.begin(), v.end(), [x](int t) { return t == x; });
    };
    CHECK(todos(tunelesS1, 0) && todos(tunelesS2, 1),
          "silvy: cada sesión va por su túnel en todas sus llamadas (objetivo, bucle y cierre)");
    CHECK(!fuga, "silvy: lo que recibe el modelo en una sesión no trae nada de otra");
}

}  // namespace pruebas_agente
