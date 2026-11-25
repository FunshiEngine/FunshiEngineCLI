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
    else if (power_ == "xhigh") effectiveMaxTokens_ = std::max(maxTokens_, 8192);
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
    if (thinking_ != "off") {
        const std::string base = baseUrl_;
        const bool esGoogle = base.find("generativelanguage.googleapis.com") != std::string::npos;
        const bool esNvidia = base.find("integrate.api.nvidia.com") != std::string::npos;
        const bool esOpenAi = base.find("api.openai.com") != std::string::npos;
        if (esOpenAi || esNvidia || isLocalhostUrl()) {
            body["reasoning_effort"] = thinking_;
        } else if (esGoogle) {
            // Gemini OpenAI-compatible: reasoning_effort es aceptado en modelos recientes;
            // mantener fallback seguro sin romper si el endpoint lo ignora/rechaza por modelo.
            body["reasoning_effort"] = thinking_;
        }
        // Otros providers custom: no enviar por defecto para evitar 400 por incompatibilidad.
    }
    if (isLocalhostUrl()) {
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
    std::vector<std::string> out;
    for (const auto& m : listModelsInfo()) out.push_back(m.id);
    return out;
}

std::vector<LlmGateway::ModelInfo> OpenAiCompatibleGateway::listModelsInfo() {
    CURL* c = curl_easy_init();
    if (!c) throw LlmException(LlmException::Code::TRANSPORT, "No se pudo inicializar libcurl");
    std::string url = baseUrl_ + "/models";
    std::string responseBuffer;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Accept: application/json");
    if (!apiKey_.empty()) hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + apiKey_).c_str());
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &responseBuffer);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(c);
    long httpCode = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) throw transportError(curl_easy_strerror(res));
    if (httpCode != 200) {
        throw LlmException::fromHttp(static_cast<int>(httpCode), responseBuffer, isLocalhostUrl());
    }

    try {
        auto json = nlohmann::json::parse(responseBuffer);
        std::vector<ModelInfo> models;
        if (json.contains("data") && json["data"].is_array()) {
            for (const auto& item : json["data"]) {
                if (!item.contains("id") || !item["id"].is_string()) continue;
                ModelInfo m;
                m.id = item["id"].get<std::string>();
                m.raw = item;
                if (item.value("reasoning", false) || item.value("supports_reasoning", false)) m.razonamiento = true;
                if (item.contains("capabilities") && item["capabilities"].is_object()) {
                    const auto& c = item["capabilities"];
                    m.razonamiento = m.razonamiento || c.value("reasoning", false) || c.value("thinking", false);
                }
                if (item.contains("supported_reasoning_efforts") && item["supported_reasoning_efforts"].is_array()) {
                    m.razonamiento = true;
                    for (const auto& v : item["supported_reasoning_efforts"]) if (v.is_string()) m.variantes.push_back(v.get<std::string>());
                }
                if (m.razonamiento && m.variantes.empty()) m.variantes = {"low", "medium", "high"};
                models.push_back(std::move(m));
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
