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

#include "LlmTypes.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <curl/curl.h>

namespace funshi::agente::llm {

/**
 * Excepción tipada del gateway LLM.
 */
class LlmException : public std::exception {
public:
    enum class Code {
        OK = 0,
        AUTH,           // 401 - API key inválida
        QUOTA,          // 429 - Cuota agotada / rate limit
        CONTEXT,        // 400+ - Contexto demasiado largo
        SERVER,         // 5xx - Error del servidor
        TIMEOUT,        // Timeout de la petición
        TRANSPORT,      // Error de red / curl
        INVALID_REQUEST,// 400 - Petición malformed
        UNKNOWN
    };

    LlmException(Code c, std::string msg, bool retryable = false, std::exception_ptr cause = nullptr)
        : code_(c), message_(std::move(msg)), retryable_(retryable), cause_(cause) {}

    Code code() const { return code_; }
    const char* what() const noexcept override { return message_.c_str(); }
    const std::string& message() const { return message_; }
    bool retryable() const { return retryable_; }
    std::exception_ptr cause() const { return cause_; }

    static LlmException fromHttp(int status, const std::string& body, bool local = false);

private:
    Code code_;
    std::string message_;
    bool retryable_;
    std::exception_ptr cause_;
};

/**
 * Contrato que usa el AgentLoop. No sabe si detrás hay Ollama, LM Studio, OpenAI, etc.
 */
class LlmGateway {
public:
    virtual ~LlmGateway() = default;

    /**
     * Streaming de la respuesta.
     * @param messages Historial de conversación
     * @param tools Catálogo opcional de herramientas (formato OpenAI)
     * @param onChunk Callback para cada chunk (text o tool_call)
     * @return true si completó OK, false si error (lanza LlmException en errores graves)
     */
    virtual bool chatStream(const std::vector<ChatMessage>& messages,
                            const std::vector<ToolSpec>* tools,
                            std::function<void(const LlmChunk&)> onChunk) = 0;

    /**
     * Sólo los fragmentos de texto (lo que pinta el chat sin herramientas).
     */
    virtual std::string chatText(const std::vector<ChatMessage>& messages) = 0;

    /**
     * Respuesta completa (sin streaming, acumula todo).
     */
    virtual std::string chat(const std::vector<ChatMessage>& messages) = 0;

    /**
     * Lista los modelos del endpoint (GET /models). Sirve para "Probar conexión".
     */
    virtual std::vector<std::string> listModels() = 0;

    virtual void close() = 0;
};

/**
 * Cliente para cualquier endpoint /v1/chat/completions con streaming SSE.
 * Compatible con: Ollama, LM Studio, vLLM, OpenAI, Groq, Together, etc.
 */
class OpenAiCompatibleGateway : public LlmGateway {
public:
    OpenAiCompatibleGateway(const std::string& baseUrl,
                            const std::string& apiKey,
                            const std::string& model,
                            double temperature = 0.7,
                            int maxTokens = 4096,
                            long chatTimeoutMs = 0,
                            const std::string& thinking = "off",
                            const std::string& power = "medium");

    bool chatStream(const std::vector<ChatMessage>& messages,
                    const std::vector<ToolSpec>* tools,
                    std::function<void(const LlmChunk&)> onChunk) override;

    std::string chatText(const std::vector<ChatMessage>& messages) override;
    std::string chat(const std::vector<ChatMessage>& messages) override;
    std::vector<std::string> listModels() override;
    void close() override;

    ~OpenAiCompatibleGateway() override;

private:
    std::string baseUrl_;
    std::string apiKey_;
    std::string model_;
    double temperature_;
    int maxTokens_;
    long chatTimeoutMs_;
    std::string thinking_;
    std::string power_;
    int effectiveMaxTokens_;

    // curl handle reutilizable
    void* curl_ = nullptr;
    struct curl_slist* headers_ = nullptr;

    void initCurl();
    void cleanupCurl();
    std::string buildRequestBody(const std::vector<ChatMessage>& messages,
                                 const std::vector<ToolSpec>* tools,
                                 bool stream) const;
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static int progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                curl_off_t ultotal, curl_off_t ulnow);
    LlmException transportError(const std::string& msg, std::exception_ptr cause = nullptr);
    bool isLocalhostUrl() const;
};

} // namespace funshi::agente::llm