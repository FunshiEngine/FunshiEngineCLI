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

// AgentLoop: bucle modelo → herramientas → modelo. El saneado de tool calls
// está en ToolCallSanitizer.cpp y el modo prompt en PromptToolParser.cpp.
#include "Agente/LLM/Bucle/AgentLoop.h"
#include "Util/LogCrash.h"
#include "Agente/LLM/Cliente/LlmGateway.h"
#include "Agente/LLM/Bucle/SeparadorPensamiento.h"
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <map>
#include <regex>
#include <vector>

namespace funshi::agente::llm {

namespace {

std::string segundos(long ms) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f s", static_cast<double>(ms) / 1000.0);
    return buf;
}

// "ronda 2 · 3.4 s (1.er token 1.2 s) · 840 → 56 tokens"
std::string resumenLlamada(int ronda, const EstadisticasLlamada& e) {
    std::string s = "ronda " + std::to_string(ronda);
    if (e.msTotal >= 0) s += " · " + segundos(e.msTotal);
    if (e.msPrimerToken >= 0) s += " (1.er token " + segundos(e.msPrimerToken) + ")";
    if (e.tokensEntrada >= 0 && e.tokensSalida >= 0) {
        s += " · " + std::to_string(e.tokensEntrada) + " → " + std::to_string(e.tokensSalida) + " tokens";
    }
    return s;
}

}  // namespace

AgentLoop::AgentLoop(std::shared_ptr<LlmGateway> gateway,
                     std::shared_ptr<ToolRegistry> registry,
                     bool nativeToolCalls,
                     long toolTimeoutMs)
    : gateway_(std::move(gateway)), registry_(std::move(registry)),
      nativeToolCalls_(nativeToolCalls), toolTimeoutMs_(toolTimeoutMs) {}

AgentAnswer AgentLoop::run(std::vector<ChatMessage>& messages,
                           std::function<void(const AgentEvent&)> onEvent,
                           const std::string& contextoTurno, const std::string& trasPedido) {
    AgentAnswer answer;
    // El prompt de sistema se renueva en cada turno: así un cambio de
    // instrucciones o de herramientas llega también a historiales viejos.
    if (!messages.empty() && messages.front().role == "system") {
        messages.front().content = buildSystemPrompt();
    } else {
        messages.insert(messages.begin(), ChatMessage::system(buildSystemPrompt()));
    }

    // Contexto del turno: se antepone al pedido y se quita al terminar.
    // npos = no hay nada que restaurar (sin contexto del turno).
    std::size_t indicePedido = std::string::npos;
    std::string pedidoOriginal;
    // El pedido del turno, con o sin contexto (para las aclaraciones EnPedido).
    const std::size_t indicePedidoTurno =
        !messages.empty() && messages.back().role == "user" ? messages.size() - 1 : std::string::npos;
    if ((!contextoTurno.empty() || !trasPedido.empty()) && !messages.empty() && messages.back().role == "user") {
        indicePedido = messages.size() - 1;
        pedidoOriginal = messages.back().content;
        messages.back().content = contextoTurno + (contextoTurno.empty() ? "" : "\n") + encabezadoPedido_ + "\n" +
                                  pedidoOriginal + (trasPedido.empty() ? "" : "\n" + trasPedido);
    }
    struct Restaurar {
        std::vector<ChatMessage>& m;
        std::size_t i;
        const std::string& original;
        ~Restaurar() { if (i < m.size()) m[i].content = original; }
    } restaurar{messages, indicePedido, pedidoOriginal};
    // La documentación que acompañó a las aclaraciones vale solo en el turno.
    recortesTurno_.clear();
    struct RecortarDocumentacion {
        std::vector<ChatMessage>& m;
        std::vector<std::pair<std::size_t, std::size_t>>& r;
        ~RecortarDocumentacion() {
            for (const auto& [i, largo] : r) {
                if (i < m.size() && m[i].content.size() > largo) m[i].content.resize(largo);
            }
            r.clear();
        }
    } recortar{messages, recortesTurno_};

    onEventActual_ = onEvent;
    const auto emitir = [&](const AgentEvent& e) {
        if (onEvent) onEvent(e);
    };
    if (cancel_) {
        if (auto* gw = dynamic_cast<OpenAiCompatibleGateway*>(gateway_.get())) gw->setCancelFlag(cancel_);
    }

    // Corta el turno si una misma herramienta falla 3 veces: los modelos
    // chicos tienden a repetir la misma llamada mal formada hasta agotar rondas.
    std::map<std::string, int> fallos;
    std::string motivoCorte;
    const auto registrarFallo = [&](const std::string& nombre, const ToolResult& r) {
        if (r.ok()) return;
        if (++fallos[nombre] >= 3 && motivoCorte.empty()) {
            motivoCorte = "\n[Se detuvo: " + nombre + " falló 3 veces en este pedido. Último error: " +
                          r.render() + "]";
        }
    };

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        if (!motivoCorte.empty()) {
            answer.text += motivoCorte;
            messages.push_back(ChatMessage::assistant(motivoCorte));
            emitir(AgentEvent::estado(motivoCorte.substr(1)));
            break;
        }
        if (cancel_ && cancel_->load()) {
            answer.text += "\n[Cancelado]";
            emitir(AgentEvent::estado("Cancelado por el usuario"));
            break;
        }
        incorporarAclaraciones(messages, indicePedidoTurno, pedidoOriginal, answer, emitir);
        std::string accumulatedText;
        std::vector<SanitizedToolCall> pendingNativeCalls;
        const std::string idRonda = "ronda-" + std::to_string(iteration + 1);
        emitir(AgentEvent::estado("ronda " + std::to_string(iteration + 1) + " · esperando al modelo (" +
                                      std::to_string(messages.size()) + " mensajes)",
                                  idRonda));

        // Texto y razonamiento: el razonamiento (del proveedor o entre
        // <think>) se muestra pero no entra al historial ni a la respuesta.
        SeparadorPensamiento separador;
        const auto entregar = [&](const SeparadorPensamiento::Partes& p) {
            if (!p.pensamiento.empty()) emitir(AgentEvent{AgentEvent::Type::Pensamiento, p.pensamiento});
            if (!p.texto.empty()) {
                accumulatedText += p.texto;
                emitir(AgentEvent{AgentEvent::Type::Text, p.texto});
            }
        };
        auto onChunk = [&](const LlmChunk& chunk) {
            if (chunk.type == LlmChunk::Type::Text) {
                entregar(separador.agregar(chunk.delta));
            } else if (chunk.type == LlmChunk::Type::Reasoning) {
                emitir(AgentEvent{AgentEvent::Type::Pensamiento, chunk.delta});
            } else if (chunk.type == LlmChunk::Type::ToolCall) {
                auto sanitized = ToolCallSanitizer::args(chunk.toolCallName, chunk.toolCallArgsJson);
                if (sanitized.ok()) {
                    pendingNativeCalls.push_back(sanitized);
                } else {
                    accumulatedText += "\n[Error en tool call: " + sanitized.errorReason + "]";
                    emitir(AgentEvent::estado("Llamada a herramienta inválida: " + sanitized.errorReason));
                }
            }
        };

        const auto specs = registry_->specs();
        const auto* specsPtr = nativeToolCalls_ ? &specs : nullptr;
        try {
            gateway_->chatStream(messages, specsPtr, onChunk);
            entregar(separador.terminar());
            emitir(AgentEvent::estado(resumenLlamada(iteration + 1, gateway_->ultimaLlamada()), idRonda));
        } catch (const LlmException& e) {
            entregar(separador.terminar());
            emitir(AgentEvent::estado(resumenLlamada(iteration + 1, gateway_->ultimaLlamada()), idRonda));
            if (e.retryable()) {
                std::string msg = "\n[Error reintentable: " + e.message() + "]";
                accumulatedText += msg;
                emitir(AgentEvent::estado("Error del modelo: " + e.message()));
            } else {
                answer.text += accumulatedText.empty() ? ("Error: " + e.message()) : accumulatedText;
                messages.push_back(ChatMessage::assistant(answer.text));
                emitir(AgentEvent::estado("Error del modelo: " + e.message()));
                return answer;
            }
        }

        if (nativeToolCalls_ && !pendingNativeCalls.empty()) {
            nlohmann::json toolCallsJson = nlohmann::json::array();
            for (const auto& tc : pendingNativeCalls) {
                std::string callId = tc.callId.empty() ? tc.name : tc.callId;
                toolCallsJson.push_back({
                    {"id", callId},
                    {"type", "function"},
                    {"function", {
                        {"name", tc.name},
                        {"arguments", tc.args.dump()}
                    }}
                });
            }
            messages.push_back(ChatMessage::assistantWithTools(accumulatedText, toolCallsJson));

            for (const auto& tc : pendingNativeCalls) {
                std::string callId = tc.callId.empty() ? tc.name : tc.callId;
                emitir(AgentEvent{AgentEvent::Type::ToolStart, "", callId, tc.name, tc.args.dump()});
                ToolResult result = executeTool(tc.name, tc.args, callId);
                registrarFallo(tc.name, result);
                answer.toolCalls.push_back({callId, tc.name, tc.args.dump(), result.ok(), result.render(), {}});
                emitir(AgentEvent{AgentEvent::Type::ToolEnd, "", callId, tc.name, "", result.ok(), result.render()});
                messages.push_back(ChatMessage::tool(callId, result.render(), tc.name));
            }
            continue;
        }

        if (!nativeToolCalls_) {
            PromptToolParser promptParser;
            std::string visible = promptParser.feed(accumulatedText);
            visible += promptParser.flush();
            auto blocks = promptParser.drain();
            {
                // Varias llamadas en un mismo bloque, una por línea: se separan.
                std::vector<std::string> separados;
                for (auto& b : blocks) {
                    auto partes = PromptToolParser::llamadasSinCerco(b);
                    if (partes.size() > 1) {
                        for (auto& p : partes) separados.push_back(std::move(p));
                    } else {
                        separados.push_back(std::move(b));
                    }
                }
                blocks = std::move(separados);
            }
            if (blocks.empty()) {
                // Llamada escrita sin cerco: se acepta solo si nombra una herramienta real.
                for (auto& b : PromptToolParser::llamadasSinCerco(accumulatedText)) {
                    const auto s = ToolCallSanitizer::block(b);
                    if (s.ok() && existeHerramienta(s.name)) blocks.push_back(std::move(b));
                }
            }
            if (blocks.empty()) {
                auto inferida = detalle::inferirLlamadaSinNombre(accumulatedText, *registry_);
                if (!inferida.empty()) {
                    blocks.push_back(std::move(inferida));
                    visible.clear();  // el JSON era la llamada, no texto para el usuario
                }
            }

            if (!blocks.empty()) {
                std::string toolResultsText;
                messages.push_back(ChatMessage::assistant(accumulatedText));
                bool toolCalled = false;
                std::string respuestaFinal;  // de una herramienta inventada para responder
                for (const auto& block : blocks) {
                    auto sanitized = ToolCallSanitizer::block(block);
                    if (sanitized.ok() && !existeHerramienta(sanitized.name)) {
                        const std::string r = detalle::respuestaDisfrazada(sanitized.name, sanitized.args);
                        if (!r.empty()) {
                            respuestaFinal = r;
                            continue;
                        }
                    }
                    if (!sanitized.ok()) {
                        std::string err = "\n[Error parseando tool call: " + sanitized.errorReason + "]";
                        answer.text += err;
                        toolResultsText += err + "\n";
                        emitir(AgentEvent::estado("Llamada a herramienta inválida: " + sanitized.errorReason));
                        continue;
                    }
                    toolCalled = true;
                    std::string callId = sanitized.callId.empty() ? sanitized.name : sanitized.callId;
                    emitir(AgentEvent{AgentEvent::Type::ToolStart, "", callId, sanitized.name, sanitized.args.dump()});
                    ToolResult result = executeTool(sanitized.name, sanitized.args, callId);
                    registrarFallo(sanitized.name, result);
                    answer.toolCalls.push_back({callId, sanitized.name, sanitized.args.dump(), result.ok(), result.render(), {}});
                    emitir(AgentEvent{AgentEvent::Type::ToolEnd, "", callId, sanitized.name, "", result.ok(), result.render()});
                    toolResultsText += std::string(kResultadoDe) + sanitized.name + ":\n" + result.render() + "\n\n";
                }
                if (!respuestaFinal.empty()) {
                    // El modelo dio por terminado el turno con esa "herramienta".
                    answer.text += sinMarcasDeSalida(respuestaFinal);
                    messages.push_back(ChatMessage::assistant(respuestaFinal));
                    break;
                }
                if (!visible.empty()) {
                    answer.text += visible;
                }
                if (toolCalled) {
                    // Sin tools nativas el resultado vuelve como mensaje de rol
                    // user: se marca como salida automática de las llamadas del
                    // propio asistente, para que no lo atribuya al usuario.
                    messages.push_back(ChatMessage::user(resultadosParaModelo(toolResultsText)));
                    continue;
                }
            }

            std::string finalText = sinMarcasDeSalida(visible.empty() ? accumulatedText : visible);
            answer.text += finalText;
            messages.push_back(ChatMessage::assistant(finalText));
            if (hayAclaraciones()) continue;  // una ronda más para atenderlas
            break;
        }

        answer.text += accumulatedText;
        messages.push_back(ChatMessage::assistant(accumulatedText));
        if (hayAclaraciones()) continue;
        break;
    }

    return answer;
}

std::string AgentLoop::formatToolResult(const ToolResult& result) const { return result.render(); }

} // namespace funshi::agente::llm
