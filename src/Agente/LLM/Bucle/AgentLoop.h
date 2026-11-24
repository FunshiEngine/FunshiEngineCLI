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

#include <atomic>

#include "Agente/LLM/Cliente/LlmGateway.h"
#include "Agente/LLM/Bucle/ToolRegistry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <mutex>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>

namespace funshi::agente::llm {

/**
 * Sanitizador de tool calls: repara JSON roto del modelo.
 */
struct SanitizedToolCall {
    enum class Type { VALID, INVALID } type;
    std::string name;
    nlohmann::json args;
    std::string callId;
    std::string errorReason;

    static SanitizedToolCall valid(std::string name, nlohmann::json args, std::string callId) {
        SanitizedToolCall r; r.type = Type::VALID; r.name = std::move(name); r.args = std::move(args); r.callId = std::move(callId); return r;
    }
    static SanitizedToolCall invalid(std::string reason) {
        SanitizedToolCall r; r.type = Type::INVALID; r.errorReason = std::move(reason); return r;
    }
    bool ok() const { return type == Type::VALID; }
};

class ToolCallSanitizer {
public:
    // Para modo nativo (tool_calls del stream OpenAI)
    static SanitizedToolCall args(const std::string& expectedName, const std::string& argsJson);

    // Para modo prompt (bloques ```tool_call ... ```)
    static SanitizedToolCall block(const std::string& raw);

private:
    static nlohmann::json lenientJson;
    static SanitizedToolCall parseAndValidate(const std::string& raw, const std::string* expectedName = nullptr);
    static std::string stripWrappers(const std::string& raw);
    static std::string cutToOuterBraces(const std::string& raw);
    static std::string repairJson(const std::string& raw);
    static SanitizedToolCall validateShape(const nlohmann::json& obj, const std::string* expectedName);
};

/**
 * Parser incremental para modo prompt (línea a línea).
 */
class PromptToolParser {
public:
    // Procesa un delta y devuelve texto visible para mostrar ya
    std::string feed(const std::string& delta);

    // Texto retenido al terminar el stream
    std::string flush();

    // Bloques completos desde la última lectura
    std::vector<std::string> drain();

    // Respaldo para modelos chicos que escriben la llamada sin el cerco
    // ```tool_call (o dentro de ```json): los objetos JSON de primer nivel del
    // texto (llaves balanceadas, respetando cadenas). Quien llama decide si
    // alguno es una llamada (debe nombrar una herramienta registrada).
    static std::vector<std::string> llamadasSinCerco(const std::string& texto);

private:
    std::string pendingLine_;
    bool inTool_ = false;
    std::string toolBuf_;
    std::vector<std::string> completed_;
    static constexpr const char* OPENER = "```tool_call";
    std::string consumeLine(const std::string& line);
};

// Mensaje con el que vuelven al modelo los resultados de sus herramientas en
// modo prompt (sin tools nativas, llegan como rol user).
std::string resultadosParaModelo(const std::string& resultados);
// Marcas de ese mensaje; sinMarcasDeSalida las quita de una respuesta que las copió.
inline constexpr const char* kInicioSalida = "[SALIDA DE TUS HERRAMIENTAS";
inline constexpr const char* kFinSalida = "[FIN DE LA SALIDA]";
inline constexpr const char* kResultadoDe = "RESULTADO DE ";
inline constexpr const char* kMarcaAclaracion = "[Aclaración del usuario";
std::string sinMarcasDeSalida(const std::string& texto);

namespace detalle {
// Respuesta que es (casi) solo los argumentos de una meta-herramienta, sin su
// nombre: devuelve la llamada reconstruida ({"tool","arguments"}) o vacío.
std::string inferirLlamadaSinNombre(const std::string& texto, const ToolRegistry& registro);
// Llamada a una herramienta inexistente que en realidad es la respuesta
// final ("fin de la respuesta", "Respuesta al usuario" con un único texto):
// devuelve ese texto, o vacío si no lo parece.
std::string respuestaDisfrazada(const std::string& herramienta, const nlohmann::json& args);
}  // namespace detalle

/**
 * Bucle principal: modelo → tool → modelo → tool...
 */
// Instrucciones que el usuario manda mientras el bucle trabaja: se
// incorporan al empezar la ronda siguiente (y si el bucle iba a terminar, da
// una ronda más para atenderlas). Seguro entre hilos.
class BuzonInstrucciones {
public:
    void agregar(std::string texto);
    std::vector<std::string> tomar();
    bool vacio() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> pendientes_;
};

// Dónde entra una aclaración en la conversación del turno.
//   MensajeNuevo  mensaje propio del usuario después de lo último;
//   EnResultados  junto a los resultados de herramientas (modo prompt) o, si
//                 no hay, como mensaje nuevo;
//   EnPedido      agregada al pedido del turno (queda en el historial);
//   EnSistema     agregada a las instrucciones del sistema del turno.
enum class FormatoAclaracion { MensajeNuevo, EnResultados, EnPedido, EnSistema };

class AgentLoop {
public:
    AgentLoop(std::shared_ptr<LlmGateway> gateway,
              std::shared_ptr<ToolRegistry> registry,
              bool nativeToolCalls = true,
              long toolTimeoutMs = 30000);

    // Máximo de rondas modelo+herramienta
    int maxIterations = 8;

    /**
     * Ejecuta el loop con callback de eventos para UI en vivo.
     */
    // `contextoTurno`: texto que acompaña solo a este pedido (p. ej. la
    // documentación recuperada por el RAG). Se antepone al último mensaje del
    // usuario mientras dura el turno y no queda en el historial: el mensaje
    // queda "<contexto>\n<encabezado del pedido>\n<pedido>\n<trasPedido>".
    AgentAnswer run(std::vector<ChatMessage>& messages,
                    std::function<void(const AgentEvent&)> onEvent = nullptr,
                    const std::string& contextoTurno = "", const std::string& trasPedido = "");

    // Config
    void setNativeToolCalls(bool v) { nativeToolCalls_ = v; }
    void setMaxIterations(int v) { maxIterations = v; }
    // Instrucciones del prompt de sistema (rol y reglas). En modo prompt se
    // les agrega la lista de herramientas y el formato de llamada.
    void setInstrucciones(std::string texto) { instrucciones_ = std::move(texto); }
    // Encabezado que separa el contexto del turno del pedido.
    void setEncabezadoPedido(std::string texto) { encabezadoPedido_ = std::move(texto); }
    // Alias de herramientas: si el modelo pide una que no existe, este
    // resolutor puede traducirla (p. ej. un comando del catálogo → la llamada
    // equivalente a ejecutar_comando). Devuelve false si no la reconoce.
    using ResolutorAlias = std::function<bool(const std::string& nombre, const nlohmann::json& args,
                                              std::string& nombreReal, nlohmann::json& argsReales)>;
    void setResolutorAlias(ResolutorAlias r) { alias_ = std::move(r); }
    // Cancela el turno en curso (el gateway corta la transferencia).
    void setCancelFlag(std::shared_ptr<std::atomic<bool>> flag) { cancel_ = std::move(flag); }
    // Aclaraciones del usuario durante el turno y dónde se incorporan.
    void setBuzon(std::shared_ptr<BuzonInstrucciones> buzon) { buzon_ = std::move(buzon); }
    void setFormatoAclaracion(FormatoAclaracion f) { formatoAclaracion_ = f; }
    // Documentación para una aclaración (la del RAG para su texto): va con
    // ella durante el turno y no queda en el historial.
    using DocumentacionPara = std::function<std::string(const std::string&)>;
    void setDocumentacionAclaraciones(DocumentacionPara d) { documentacionAclaracion_ = std::move(d); }
    // Quién decide los permisos que piden las herramientas (borrar, por
    // ejemplo). Sin esto, se niegan.
    using SolicitudPermiso = std::function<PermissionDecision(const PermissionRequest&)>;
    void setSolicitudPermiso(SolicitudPermiso s) { solicitudPermiso_ = std::move(s); }

private:
    std::shared_ptr<LlmGateway> gateway_;
    std::shared_ptr<ToolRegistry> registry_;
    bool nativeToolCalls_;
    long toolTimeoutMs_;
    std::string instrucciones_ =
        "Eres un asistente que controla el motor FunshiEngine. "
        "Puedes usar herramientas para crear proyectos, escenas, objetos, scripts, etc.";
    std::string encabezadoPedido_ = "[Pedido del usuario]";
    std::shared_ptr<BuzonInstrucciones> buzon_;
    FormatoAclaracion formatoAclaracion_ = FormatoAclaracion::MensajeNuevo;
    DocumentacionPara documentacionAclaracion_;
    SolicitudPermiso solicitudPermiso_;
    // Mensajes a los que se agregó documentación en el turno: (índice,
    // largo sin ella). Al terminar el turno se recortan.
    std::vector<std::pair<std::size_t, std::size_t>> recortesTurno_;
    // Incorpora las aclaraciones pendientes; true si había alguna.
    bool incorporarAclaraciones(std::vector<ChatMessage>& messages, std::size_t indicePedido,
                                std::string& pedidoOriginal, AgentAnswer& answer,
                                const std::function<void(const AgentEvent&)>& emitir);
    bool hayAclaraciones() const { return buzon_ && !buzon_->vacio(); }
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::function<void(const AgentEvent&)> onEventActual_;  // durante run()
    ResolutorAlias alias_;
    bool existeHerramienta(const std::string& nombre) const;
    std::string nombreParecido(const std::string& nombre) const;

    std::string buildSystemPrompt() const;
    ToolResult executeTool(const std::string& name, const nlohmann::json& args, const std::string& callId);
    std::string formatToolResult(const ToolResult& result) const;
};

} // namespace funshi::agente::llm