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

// OpenAiCompatibleGateway: conexión cURL, cuerpo de petición y llamadas no
// streaming. El streaming SSE está en LlmGateway_Stream.cpp.
#include "Agente/LLM/Cliente/LlmGateway.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>

#include "Util/Metricas.h"

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
    // XFERINFO usa curl_off_t (PROGRESSFUNCTION, la antigua, usa double).
    curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, this);
    // Sin señales: imprescindible al usar cURL desde hilos.
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
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
        // El catálogo casi nunca cambia entre vueltas: se serializa una vez
        // (cada ToolSpec::toJson vuelve a parsear su esquema JSON).
        std::string clave;
        for (const auto& t : *tools) {
            clave += t.name;
            clave += '\x1f';
            clave += t.parametersSchema;
            clave += '\x1e';
        }
        if (clave != toolsKey_) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& t : *tools) arr.push_back(t.toJson());
            toolsJson_ = std::move(arr);
            toolsKey_ = std::move(clave);
        }
        body["tools"] = toolsJson_;
        body["tool_choice"] = "auto";
    }
    if (thinking_ != "off") body["reasoning_effort"] = thinking_;
    if (isLocalhostUrl()) {
        // llama-server: reutilizar la caché KV del prefijo común (historial) y
        // devolver el uso de tokens al final del stream.
        body["cache_prompt"] = true;
        if (stream) body["stream_options"] = {{"include_usage", true}};
        if (tunel_ >= 0) body["id_slot"] = tunel_;
    }
    return body.dump();
}

size_t OpenAiCompatibleGateway::writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

int OpenAiCompatibleGateway::progressCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    // Devolver != 0 aborta la transferencia: así se cancela una respuesta en curso.
    auto* self = static_cast<OpenAiCompatibleGateway*>(clientp);
    return (self && self->cancel_ && self->cancel_->load()) ? 1 : 0;
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
