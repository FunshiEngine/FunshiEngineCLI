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

#include "LlmGateway.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace funshi::agente::llm {

LlmException LlmException::fromHttp(int status, const std::string& body, bool local) {
    Code code = Code::UNKNOWN;
    bool retryable = false;
    std::string msg;

    if (status == 401) {
        code = Code::AUTH;
        msg = local ? "API key inválida o faltante (endpoint local sin auth configurado)" : "API key inválida (401)";
    } else if (status == 429) {
        code = Code::QUOTA;
        msg = "Cuota agotada o rate limit (429)";
        retryable = true;
    } else if (status >= 400 && status < 500) {
        std::string lower = body;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("context") != std::string::npos ||
            lower.find("max_tokens") != std::string::npos ||
            lower.find("token limit") != std::string::npos ||
            lower.find("too long") != std::string::npos) {
            code = Code::CONTEXT;
            msg = "Contexto demasiado largo para el modelo (400): " + body;
        } else {
            code = Code::INVALID_REQUEST;
            msg = "Petición inválida (400): " + body;
        }
    } else if (status >= 500) {
        code = Code::SERVER;
        msg = "Error del servidor (" + std::to_string(status) + "): " + body;
        retryable = true;
    } else {
        msg = "HTTP " + std::to_string(status) + ": " + body;
    }
    return LlmException(code, msg, retryable);
}

OpenAiCompatibleGateway::OpenAiCompatibleGateway(const std::string& baseUrl,
                                                 const std::string& apiKey,
                                                 const std::string& model,
                                                 double temperature,
                                                 int maxTokens,
                                                 long chatTimeoutMs,
                                                 const std::string& thinking,
                                                 const std::string& power)
    : baseUrl_(baseUrl), apiKey_(apiKey), model_(model),
      temperature_(temperature), maxTokens_(maxTokens),
      chatTimeoutMs_(chatTimeoutMs), thinking_(thinking), power_(power) {

    if (power_ == "low") effectiveMaxTokens_ = 256;
    else if (power_ == "high") effectiveMaxTokens_ = std::max(maxTokens_, 4096);
    else effectiveMaxTokens_ = maxTokens_;

    initCurl();
}

OpenAiCompatibleGateway::~OpenAiCompatibleGateway() { close(); }

void OpenAiCompatibleGateway::initCurl() {
    curl_ = curl_easy_init();
    if (!curl_) throw LlmException(LlmException::Code::TRANSPORT, "No se pudo inicializar libcurl");

    headers_ = curl_slist_append(headers_, "Content-Type: application/json");
    headers_ = curl_slist_append(headers_, "Accept: text/event-stream");
    if (!apiKey_.empty()) {
        headers_ = curl_slist_append(headers_, ("Authorization: Bearer " + apiKey_).c_str());
    }

    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl_, CURLOPT_PROGRESSFUNCTION, progressCallback);
    curl_easy_setopt(curl_, CURLOPT_PROGRESSDATA, this);
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 5L);
}

void OpenAiCompatibleGateway::cleanupCurl() {
    if (headers_) { curl_slist_free_all(headers_); headers_ = nullptr; }
    if (curl_) { curl_easy_cleanup(curl_); curl_ = nullptr; }
}

std::string OpenAiCompatibleGateway::buildRequestBody(const std::vector<ChatMessage>& messages,
                                                      const std::vector<ToolSpec>* tools,
                                                      bool stream) const {
    nlohmann::json body;
    body["model"] = model_;
    body["messages"] = nlohmann::json::array();
    for (const auto& m : messages) body["messages"].push_back(m.toJson());
    body["temperature"] = temperature_;
    body["max_tokens"] = effectiveMaxTokens_;
    body["stream"] = stream;

    if (tools && !tools->empty()) {
        body["tools"] = nlohmann::json::array();
        for (const auto& t : *tools) body["tools"].push_back(t.toJson());
        body["tool_choice"] = "auto";
    }
    if (thinking_ != "off") body["reasoning_effort"] = thinking_;
    return body.dump();
}

size_t OpenAiCompatibleGateway::writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

int OpenAiCompatibleGateway::progressCallback(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return 0;
}

// ============================================================
// StreamState para chatStream
// ============================================================

struct StreamState {
    std::function<void(const LlmChunk&)> onChunk;
    std::string buffer;
    bool inToolCall = false;
    int toolCallIndex = -1;
    std::string currentToolCallId;
    std::string currentToolCallName;
    std::string currentToolCallArgs;
};

static size_t streamWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    StreamState* state = static_cast<StreamState*>(userdata);
    size_t total = size * nmemb;
    state->buffer.append(ptr, total);

    size_t pos;
    while ((pos = state->buffer.find("\n\n")) != std::string::npos) {
        std::string event = state->buffer.substr(0, pos);
        state->buffer.erase(0, pos + 2);

        if (event.rfind("data: ", 0) == 0) {
            std::string data = event.substr(6);
            if (data == "[DONE]") continue;

            try {
                auto json = nlohmann::json::parse(data);
                auto choices = json.value("choices", nlohmann::json::array());
                if (!choices.empty()) {
                    auto delta = choices[0].value("delta", nlohmann::json::object());

                    if (delta.contains("content") && !delta["content"].is_null()) {
                        std::string text = delta["content"].get<std::string>();
                        if (!text.empty()) {
                            state->onChunk(LlmChunk::makeText(text));
                        }
                    }

                    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                        for (const auto& tc : delta["tool_calls"]) {
                            int idx = tc.value("index", 0);
                            if (idx != state->toolCallIndex) {
                                if (state->inToolCall && !state->currentToolCallName.empty()) {
                                    state->onChunk(LlmChunk::makeToolCall(
                                        state->currentToolCallId.empty() ? state->currentToolCallName : state->currentToolCallId,
                                        state->currentToolCallName,
                                        state->currentToolCallArgs.empty() ? "{}" : state->currentToolCallArgs
                                    ));
                                }
                                state->toolCallIndex = idx;
                                state->inToolCall = true;
                                state->currentToolCallId.clear();
                                state->currentToolCallName.clear();
                                state->currentToolCallArgs.clear();
                            }
                            if (tc.contains("id") && !tc["id"].is_null()) {
                                state->currentToolCallId += tc["id"].get<std::string>();
                            }
                            if (tc.contains("function")) {
                                auto fn = tc["function"];
                                if (fn.contains("name") && !fn["name"].is_null()) {
                                    state->currentToolCallName += fn["name"].get<std::string>();
                                }
                                if (fn.contains("arguments") && !fn["arguments"].is_null()) {
                                    state->currentToolCallArgs += fn["arguments"].get<std::string>();
                                }
                            }
                        }
                    }
                }
            } catch (const std::exception&) {
                // Ignorar JSON inválido
            }
        }
    }
    return total;
}

bool OpenAiCompatibleGateway::chatStream(const std::vector<ChatMessage>& messages,
                                         const std::vector<ToolSpec>* tools,
                                         std::function<void(const LlmChunk&)> onChunk) {
    if (!curl_) initCurl();

    std::string url = baseUrl_ + "/chat/completions";
    std::string body = buildRequestBody(messages, tools, true);

    StreamState state{std::move(onChunk), "", false, -1, "", "", ""};

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, streamWriteCallback);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, chatTimeoutMs_ > 0 ? chatTimeoutMs_ : 0);

    CURLcode res = curl_easy_perform(curl_);

    if (state.inToolCall && !state.currentToolCallName.empty()) {
        state.onChunk(LlmChunk::makeToolCall(
            state.currentToolCallId.empty() ? state.currentToolCallName : state.currentToolCallId,
            state.currentToolCallName,
            state.currentToolCallArgs.empty() ? "{}" : state.currentToolCallArgs
        ));
    }

    if (res == CURLE_OPERATION_TIMEDOUT) {
        throw LlmException(LlmException::Code::TIMEOUT,
            "La petición superó " + std::to_string(chatTimeoutMs_) + " ms", true);
    }
    if (res != CURLE_OK) {
        throw transportError(curl_easy_strerror(res));
    }

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        throw LlmException::fromHttp(static_cast<int>(httpCode), state.buffer, isLocalhostUrl());
    }
    return true;
}

std::string OpenAiCompatibleGateway::chatText(const std::vector<ChatMessage>& messages) {
    std::string result;
    chatStream(messages, nullptr, [&](const LlmChunk& chunk) {
        if (chunk.type == LlmChunk::Type::Text) result += chunk.delta;
    });
    return result;
}

std::string OpenAiCompatibleGateway::chat(const std::vector<ChatMessage>& messages) {
    return chatText(messages);
}

std::vector<std::string> OpenAiCompatibleGateway::listModels() {
    if (!curl_) initCurl();

    std::string url = baseUrl_ + "/models";
    std::string responseBuffer;

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, nullptr);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,
        [](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            auto* buf = static_cast<std::string*>(userdata);
            buf->append(ptr, size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &responseBuffer);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) throw transportError(curl_easy_strerror(res));

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        throw LlmException::fromHttp(static_cast<int>(httpCode), responseBuffer, isLocalhostUrl());
    }

    try {
        auto json = nlohmann::json::parse(responseBuffer);
        std::vector<std::string> models;
        if (json.contains("data") && json["data"].is_array()) {
            for (const auto& item : json["data"]) {
                if (item.contains("id") && item["id"].is_string()) {
                    models.push_back(item["id"].get<std::string>());
                }
            }
        }
        return models;
    } catch (const std::exception& e) {
        throw LlmException(LlmException::Code::TRANSPORT, "Error parseando /models: " + std::string(e.what()));
    }
}

void OpenAiCompatibleGateway::close() { cleanupCurl(); }

LlmException OpenAiCompatibleGateway::transportError(const std::string& msg, std::exception_ptr cause) {
    return LlmException(LlmException::Code::TRANSPORT, "Error de red: " + msg, true, cause);
}

bool OpenAiCompatibleGateway::isLocalhostUrl() const {
    if (baseUrl_.empty()) return false;
    try {
        size_t proto = baseUrl_.find("://");
        size_t start = (proto == std::string::npos) ? 0 : proto + 3;
        size_t end = baseUrl_.find(':', start);
        if (end == std::string::npos) end = baseUrl_.find('/', start);
        if (end == std::string::npos) end = baseUrl_.size();
        std::string host = baseUrl_.substr(start, end - start);
        std::transform(host.begin(), host.end(), host.begin(), ::tolower);
        return host == "localhost" || host == "127.0.0.1" || host == "::1" ||
               (host.size() >= 10 && host.compare(host.size() - 10, 10, ".localhost") == 0);
    } catch (...) { return false; }
}

} // namespace funshi::agente::llm