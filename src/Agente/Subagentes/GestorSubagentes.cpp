/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Subagentes/GestorSubagentes.h"

#include "Agente/Conocimiento/BaseConocimiento.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <future>
#include <mutex>

namespace funshi::agente {

namespace {

std::string unir(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

}  // namespace

std::string GestorSubagentes::nuevoId() {
    // Único entre pedidos y reinicios: segundos + contador del proceso.
    static std::atomic<unsigned> contador{0};
    const auto seg = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return "sa-" + std::to_string(seg) + "-" + std::to_string(++contador);
}

GestorSubagentes::GestorSubagentes(FabricaBucles fabrica, int maxParalelo, std::string proyectoActivo)
    : fabrica_(std::move(fabrica)), maxParalelo_(std::max(1, maxParalelo)), proyectoActivo_(std::move(proyectoActivo)) {}

InformeSubagente GestorSubagentes::ejecutarUna(const TareaSubagente& tarea, std::size_t indice,
                                               std::size_t total,
                                               const std::function<void(const llm::AgentEvent&)>& emitir) {
    using Tipo = llm::AgentEvent::Type;
    InformeSubagente informe;
    informe.titulo = tarea.titulo;
    informe.perfil = tarea.perfil;
    informe.id = nuevoId();

    // Inicio: la UI recibe la tarea completa para mostrarla en la vista del subagente.
    llm::AgentEvent inicio{Tipo::SubagenteInicio};
    inicio.name = tarea.titulo;
    inicio.origen = informe.id;
    inicio.argsJson = nlohmann::json{{"titulo", tarea.titulo},
                                     {"instrucciones", tarea.instrucciones},
                                     {"contexto", tarea.contexto},
                                     {"perfil", tarea.perfil},
                                     {"proyecto", proyectoActivo_},
                                     {"indice", indice + 1},
                                     {"total", total}}
                          .dump();
    emitir(inicio);

    // Todo lo del subagente (texto, razonamiento, herramientas, estado) sale
    // con su id como origen: la UI lo separa en su propia vista.
    auto reenviar = [&](const llm::AgentEvent& e) {
        llm::AgentEvent copia = e;
        copia.origen = informe.id;
        emitir(copia);
    };
    try {
        BucleCreado creado = fabrica_(tarea.perfil);
        if (!creado.bucle) throw std::runtime_error("no se pudo crear el subagente");
        // La documentación va en las instrucciones (no en el mensaje): los
        // modelos chicos tienden a repetir el mensaje como si fuera el informe.
        const std::string pedido = tarea.instrucciones + "\n" + tarea.contexto;
        std::vector<std::string> usados;
        const std::string documentacion = conocimiento::BaseConocimiento::global()->contextoParaPedido(
            pedido, creado.herramientas, 3, &usados);
        reenviar(llm::AgentEvent::estado(usados.empty() ? "documentación: ninguna relevante"
                                                        : "documentación: " + unir(usados)));
        creado.bucle->setInstrucciones(instruccionesSubagente(tarea, proyectoActivo_) +
                                       (documentacion.empty() ? "" : "\n\n" + documentacion));
        creado.bucle->setMaxIterations(kMaxRondas);

        std::vector<llm::ChatMessage> historial{
            llm::ChatMessage::user("Realiza ahora la TAREA usando las herramientas y termina con el informe.")};
        const llm::AgentAnswer respuesta = creado.bucle->run(historial, reenviar);

        informe.informe = respuesta.text;
        informe.herramientasUsadas = static_cast<int>(respuesta.toolCalls.size());
        std::string bajo = informe.informe;
        std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char c) { return std::tolower(c); });
        informe.ok = !informe.informe.empty() && bajo.find("resultado: fallido") == std::string::npos;
    } catch (const std::exception& e) {
        informe.ok = false;
        informe.informe = std::string("RESULTADO: fallido\nPROBLEMAS: ") + e.what();
    }
    if (informe.informe.size() > kMaxInforme) {
        informe.informe = informe.informe.substr(0, kMaxInforme) + "…[informe recortado]";
    }
    llm::AgentEvent fin{Tipo::SubagenteFin};
    fin.name = tarea.titulo;
    fin.origen = informe.id;
    fin.ok = informe.ok;
    fin.result = informe.informe;
    emitir(fin);
    return informe;
}

std::vector<InformeSubagente> GestorSubagentes::ejecutar(
    const std::vector<TareaSubagente>& tareas,
    const std::function<void(const llm::AgentEvent&)>& onEvent) {
    std::mutex mutexEventos;
    const auto emitir = [&](const llm::AgentEvent& e) {
        if (!onEvent) return;
        std::lock_guard<std::mutex> lock(mutexEventos);
        onEvent(e);
    };
    const std::size_t total = std::min(tareas.size(), kMaxTareas);
    std::vector<InformeSubagente> informes(total);
    // Tandas de `maxParalelo_` subagentes: cada uno en su hilo, con su gateway.
    for (std::size_t inicio = 0; inicio < total; inicio += static_cast<std::size_t>(maxParalelo_)) {
        const std::size_t fin = std::min(total, inicio + static_cast<std::size_t>(maxParalelo_));
        std::vector<std::future<InformeSubagente>> enCurso;
        for (std::size_t i = inicio; i < fin; ++i) {
            if (fin - inicio == 1) {
                informes[i] = ejecutarUna(tareas[i], i, total, emitir);
            } else {
                enCurso.push_back(std::async(std::launch::async, [&, i]() {
                    return ejecutarUna(tareas[i], i, total, emitir);
                }));
            }
        }
        for (std::size_t j = 0; j < enCurso.size(); ++j) informes[inicio + j] = enCurso[j].get();
    }
    return informes;
}

std::string GestorSubagentes::resumen(const std::vector<InformeSubagente>& informes) {
    std::string s = "Informes de " + std::to_string(informes.size()) + " subagente(s):\n";
    for (std::size_t i = 0; i < informes.size(); ++i) {
        const auto& inf = informes[i];
        s += "\n### [" + std::to_string(i + 1) + "] " + inf.titulo + " (perfil " + inf.perfil + ", " +
             std::to_string(inf.herramientasUsadas) + " herramientas)\n" + inf.informe + "\n";
    }
    s += "\nVerifica lo importante antes de confirmar al usuario.";
    return s;
}

}  // namespace funshi::agente
