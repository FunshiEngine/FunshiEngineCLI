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

// OpenAiCompatibleGateway::chatStream: parseo incremental de SSE (texto,
// tool calls y uso) con cancelación.
#include "Agente/LLM/Cliente/LlmGateway.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>

#include "Util/Metricas.h"

namespace funshi::agente::llm {

// ============================================================
// StreamState para chatStream
// ============================================================

namespace {

struct StreamState {
    std::function<void(const LlmChunk&)> onChunk;
    std::string buffer;      // bytes recibidos aún no procesados (desde `leido`)
    std::size_t leido = 0;   // offset de lectura: evita borrar del inicio en cada evento
    std::string cuerpoCrudo; // primeros bytes de la respuesta, para mensajes de error
    bool inToolCall = false;
    int toolCallIndex = -1;
    std::string currentToolCallId;
    std::string currentToolCallName;
    std::string currentToolCallArgs;
    // Métricas
    std::chrono::steady_clock::time_point inicio;
    std::chrono::steady_clock::time_point primerToken{};
    bool hayPrimerToken = false;
    std::size_t caracteres = 0;
    long promptTokens = -1;
    long completionTokens = -1;
};

void emitirToolCallPendiente(StreamState& st) {
    if (st.inToolCall && !st.currentToolCallName.empty()) {
        st.onChunk(LlmChunk::makeToolCall(
            st.currentToolCallId.empty() ? st.currentToolCallName : st.currentToolCallId,
            st.currentToolCallName,
            st.currentToolCallArgs.empty() ? "{}" : st.currentToolCallArgs));
    }
}

void marcarPrimerToken(StreamState& st) {
    if (!st.hayPrimerToken) {
        st.hayPrimerToken = true;
        st.primerToken = std::chrono::steady_clock::now();
    }
}

void procesarDatoSse(StreamState& st, const char* datos, std::size_t largo) {
    if (largo == 6 && std::memcmp(datos, "[DONE]", 6) == 0) return;
    nlohmann::json json = nlohmann::json::parse(datos, datos + largo, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return;

    if (auto u = json.find("usage"); u != json.end() && u->is_object()) {
        st.promptTokens = u->value("prompt_tokens", st.promptTokens);
        st.completionTokens = u->value("completion_tokens", st.completionTokens);
    }
    auto choices = json.find("choices");
    if (choices == json.end() || !choices->is_array() || choices->empty()) return;
    const nlohmann::json& choice = (*choices)[0];
    auto deltaIt = choice.find("delta");
    if (deltaIt == choice.end() || !deltaIt->is_object()) return;
    const nlohmann::json& delta = *deltaIt;

    if (auto c = delta.find("content"); c != delta.end() && c->is_string()) {
        const std::string& text = c->get_ref<const std::string&>();
        if (!text.empty()) {
            marcarPrimerToken(st);
            st.caracteres += text.size();
            st.onChunk(LlmChunk::makeText(text));
        }
    }

    auto tcs = delta.find("tool_calls");
    if (tcs == delta.end() || !tcs->is_array()) return;
    marcarPrimerToken(st);
    for (const auto& tc : *tcs) {
        const int idx = tc.value("index", 0);
        if (idx != st.toolCallIndex) {
            emitirToolCallPendiente(st);
            st.toolCallIndex = idx;
            st.inToolCall = true;
            st.currentToolCallId.clear();
            st.currentToolCallName.clear();
            st.currentToolCallArgs.clear();
        }
        if (auto id = tc.find("id"); id != tc.end() && id->is_string()) {
            st.currentToolCallId += id->get_ref<const std::string&>();
        }
        auto fn = tc.find("function");
        if (fn == tc.end() || !fn->is_object()) continue;
        if (auto n = fn->find("name"); n != fn->end() && n->is_string()) {
            st.currentToolCallName += n->get_ref<const std::string&>();
        }
        if (auto ar = fn->find("arguments"); ar != fn->end() && ar->is_string()) {
            st.currentToolCallArgs += ar->get_ref<const std::string&>();
        }
    }
}

size_t streamWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* st = static_cast<StreamState*>(userdata);
    const size_t total = size * nmemb;
    if (st->cuerpoCrudo.size() < 4096) {
        st->cuerpoCrudo.append(ptr, std::min(total, 4096 - st->cuerpoCrudo.size()));
    }
    st->buffer.append(ptr, total);

    // SSE línea a línea: acepta "\n" y "\r\n", con o sin espacio tras "data:".
    for (;;) {
        const std::size_t fin = st->buffer.find('\n', st->leido);
        if (fin == std::string::npos) break;
        std::size_t largo = fin - st->leido;
        const char* linea = st->buffer.data() + st->leido;
        if (largo > 0 && linea[largo - 1] == '\r') --largo;
        if (largo >= 5 && std::memcmp(linea, "data:", 5) == 0) {
            const char* datos = linea + 5;
            std::size_t n = largo - 5;
            if (n > 0 && *datos == ' ') { ++datos; --n; }
            procesarDatoSse(*st, datos, n);
        }
        st->leido = fin + 1;
    }
    // Compactar de vez en cuando (no en cada evento).
    if (st->leido > 65536 || st->leido == st->buffer.size()) {
        st->buffer.erase(0, st->leido);
        st->leido = 0;
    }
    return total;
}

}  // namespace

bool OpenAiCompatibleGateway::chatStream(const std::vector<ChatMessage>& messages,
                                         const std::vector<ToolSpec>* tools,
                                         std::function<void(const LlmChunk&)> onChunk) {
    if (!curl_) initCurl();

    const std::string url = baseUrl_ + "/chat/completions";
    const std::string body = buildRequestBody(messages, tools, true);

    StreamState state;
    state.onChunk = std::move(onChunk);
    state.inicio = std::chrono::steady_clock::now();

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, streamWriteCallback);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, chatTimeoutMs_ > 0 ? chatTimeoutMs_ : 0L);

    const CURLcode res = curl_easy_perform(curl_);
    emitirToolCallPendiente(state);

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);

    if (metricas::activas()) {
        const auto ahora = std::chrono::steady_clock::now();
        auto ms = [](auto d) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
        };
        const auto totalMs = ms(ahora - state.inicio);
        const auto genMs = state.hayPrimerToken ? ms(ahora - state.primerToken) : 0;
        metricas::registrar("llm.chat", {
            {"modelo", model_},
            {"local", isLocalhostUrl()},
            {"mensajes", messages.size()},
            {"bytesPeticion", body.size()},
            {"msPrimerToken", state.hayPrimerToken ? ms(state.primerToken - state.inicio) : -1},
            {"msTotal", totalMs},
            {"caracteres", state.caracteres},
            {"promptTokens", state.promptTokens},
            {"completionTokens", state.completionTokens},
            {"tokensPorSegundo", (state.completionTokens > 0 && genMs > 0)
                                     ? state.completionTokens * 1000.0 / static_cast<double>(genMs)
                                     : -1.0},
            {"http", httpCode},
        });
    }

    if (res == CURLE_ABORTED_BY_CALLBACK) {
        throw LlmException(LlmException::Code::TRANSPORT, "Cancelado por el usuario", false);
    }
    if (res == CURLE_OPERATION_TIMEDOUT) {
        throw LlmException(LlmException::Code::TIMEOUT,
            "La petición superó " + std::to_string(chatTimeoutMs_) + " ms", true);
    }
    if (res != CURLE_OK) {
        throw transportError(curl_easy_strerror(res));
    }
    if (httpCode != 200) {
        throw LlmException::fromHttp(static_cast<int>(httpCode), state.cuerpoCrudo, isLocalhostUrl());
    }
    return true;
}
} // namespace funshi::agente::llm
