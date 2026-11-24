/*
 * Copyright 2024 FunshiEngine
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "Agente/Tareas/TareaAgente.h"
#include "Agente/LLM/Cliente/AsignadorTuneles.h"
#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Agente/Harness.h"
#include "Nucleo/Solicitud.h"
#include "Agente/LLM/Config/LlmTypes.h"
#include "Agente/LLM/Config/LlmConfig.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <mutex>

namespace funshi::agente::llm {
    class LlmGateway;
    class ToolRegistry;
    class AgentLoop;
    struct LlmConfig;
}

namespace funshi {

// Orden procesada por Silvy: texto original, solicitud generada y si se logró
// interpretar.
struct OrdenProcesada {
    std::string textoOriginal;
    Solicitud solicitud;
    bool interpretada = false;
    std::string motivoRechazo;
};

// Estado de una conversación con el agente: su historial, el proyecto que
// el usuario tiene activo y la última tarea. Cada sesión de chat tiene la suya; varias pueden
// procesarse a la vez (cada pedido crea su propio bucle y gateway).
struct SesionAgente {
    // Identifica la sesión ante el servidor del modelo: cada una va por su
    // propio túnel (slot y caché). Vacío = la sesión de la CLI.
    std::string id;
    std::vector<agente::llm::ChatMessage> historial;
    std::string proyectoActivo;
    // Última tarea cerrada (lo que el modelo hizo y su resumen): contexto
    // del pedido siguiente.
    agente::tareas::TareaAgente tareaAnterior;
    // Las tareas de antes (la más vieja primero, hasta kMaxTareasPrevias):
    // al descartar pedidos se vuelve a la que correspondía.
    std::vector<agente::tareas::TareaAgente> tareasPrevias;
    static constexpr std::size_t kMaxTareasPrevias = 20;

    // Descarta los últimos `pedidos` pedidos al modelo (el historial desde el
    // más viejo de ellos, sus resultados y aclaraciones) y vuelve a la tarea
    // anterior a ellos. Para editar o borrar un mensaje del chat.
    void descartarPedidos(std::size_t pedidos);
    // Poner en true cancela el pedido en curso de esta sesión.
    std::shared_ptr<std::atomic<bool>> cancelar = std::make_shared<std::atomic<bool>>(false);
    // Aclaraciones para el pedido en curso: el bucle las incorpora en su
    // próxima ronda. Las que llegan cuando ya terminó quedan aquí.
    std::shared_ptr<agente::llm::BuzonInstrucciones> aclaraciones =
        std::make_shared<agente::llm::BuzonInstrucciones>();
};

// Silvy: agente que recibe texto. Si empieza con '/' ejecuta comando CLI directo.
// Si no, se lo pasa al agente LLM (si está configurado), con la documentación
// de los comandos relevantes (RAG) y la posibilidad de delegar en subagentes.
class Silvy {
public:
    // Pedido sin las menciones de proyectos existentes, para buscar en la
    // base de conocimiento (el modelo sí recibe el pedido completo).
    static std::string consultaParaConocimiento(const std::string& pedido, const std::vector<std::string>& proyectos);

    explicit Silvy(Harness& harness);

    // Configura el agente LLM (requiere libcurl + config válida).
    // Devuelve true si se configuró correctamente.
    bool configurarLlm(const agente::llm::LlmConfig& config, bool nativeToolCalls = true);

    // Crea los gateways con `fabrica` (pruebas con un modelo falso).
    using FabricaGateway = std::function<std::shared_ptr<agente::llm::LlmGateway>()>;
    void configurarConFabrica(FabricaGateway fabrica, bool nativeToolCalls, int subagentesParalelos,
                              const std::string& nombreModelo = "prueba");

    bool tieneLlm() const;

    // Procesa una línea de texto:
    // - Si empieza con '/', ejecuta como comando CLI (project.create, etc.)
    // - Si no, la envía al agente LLM (requiere tenerLlm()).
    // Devuelve la respuesta textual (para mostrar en chat) y el resultado del comando si aplica.
    struct RespuestaProcesada {
        std::string texto;           // Texto para mostrar al usuario
        ResultadoComando resultado;  // Solo válido si fue comando CLI
        bool fueComandoCli = false;
        bool error = false;
    };
    // Con la sesión por defecto (CLI, chat de línea).
    RespuestaProcesada procesar(const std::string& texto,
                                std::function<void(const agente::llm::AgentEvent&)> onEvent = nullptr);
    // Con una sesión propia (TUI con varias sesiones). Seguro entre hilos
    // siempre que cada sesión se procese de a un pedido.
    RespuestaProcesada procesar(const std::string& texto, SesionAgente& sesion,
                                std::function<void(const agente::llm::AgentEvent&)> onEvent);

    // Limpia el historial y la tarea anterior de la sesión por defecto.
    void limpiarHistorialLlm();

    // Tareas: antes de cada pedido el modelo escribe lo que debe hacer
    // (con la tarea anterior a la vista) y al terminar, lo que hizo y un
    // resumen. Activas con configurarLlm; configurarConFabrica las deja
    // apagadas salvo que se activen aquí.
    void setTareasActivas(bool activas);

    // Túneles hacia el servidor (uno por sesión). configurarLlm los crea para
    // un servidor local; aquí se reemplazan (pruebas).
    void setTuneles(std::shared_ptr<agente::llm::AsignadorTuneles> tuneles);
    // Dónde entra en la conversación una aclaración que llega durante el pedido.
    void setFormatoAclaracion(agente::llm::FormatoAclaracion formato);

    // Devuelve el resumen de auditoría para mostrar al usuario qué se hizo.
    std::string resumenQueHice(std::size_t n = 10) const;

    // Devuelve el nombre del modelo LLM activo (si hay uno configurado), vacío si no.
    std::string modeloActivo() const;

    const Harness& harness() const { return harness_; }

private:
    // Configuración del modelo compartida por todos los pedidos. Se reemplaza
    // entera al reconfigurar; los pedidos en curso conservan la suya.
    struct EstadoLlm {
        FabricaGateway fabrica;
        bool nativas = true;
        int subagentesParalelos = 1;
        long toolTimeoutMs = 30000;
        std::string modelo;
        bool tareas = true;
        std::shared_ptr<agente::llm::AsignadorTuneles> tuneles;  // nulo: sin túneles
        agente::llm::FormatoAclaracion formatoAclaracion = agente::llm::FormatoAclaracion::MensajeNuevo;
    };
    std::shared_ptr<const EstadoLlm> estadoLlm() const;

    Harness& harness_;
    std::vector<OrdenProcesada> historial_;
    mutable std::mutex mutexLlm_;
    std::shared_ptr<const EstadoLlm> llm_;
    SesionAgente sesionPorDefecto_;

    bool esJsonValido(const std::string& texto) const;
    OrdenProcesada interpretarTextoPlano(const std::string& texto);
    OrdenProcesada interpretar(const std::string& texto);
    RespuestaProcesada ejecutarComandoCli(const std::string& texto);
    RespuestaProcesada enviarAlLlm(const std::string& texto, SesionAgente& sesion,
                                   std::function<void(const agente::llm::AgentEvent&)> onEvent);
};

}  // namespace funshi
