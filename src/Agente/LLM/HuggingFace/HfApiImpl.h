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

// Interno de HfApi: estado cURL compartido por HfApi*.cpp. No incluir fuera.
#include "Agente/LLM/HuggingFace/HfApi.h"
#include "Agente/LLM/Cliente/HttpDescarga.h"

#include <curl/curl.h>

#include <functional>
#include <string>

namespace funshi::agente::llm {

namespace detalle_hf {
inline size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append(static_cast<char*>(contents), total);
    return total;
}

inline int ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* progressFn = static_cast<std::function<void(double)>*>(clientp);
    if (!progressFn) return 0;
    if (dltotal > 0 && dlnow >= 0) {
        double p = static_cast<double>(dlnow) / static_cast<double>(dltotal);
        if (p >= 0.0 && p <= 1.0) {
            (*progressFn)(p);
        }
    } else if (dlnow > 0) {
        // Indeterminate progress: server didn't provide Content-Length.
        // Report negative value to signal "downloading but unknown progress".
        (*progressFn)(-1.0);
    }
    return 0;
}

}  // namespace detalle_hf

using detalle_hf::WriteCallback;
using detalle_hf::ProgressCallback;
struct HfApi::Impl {
    CURL* curl = nullptr;
    std::string baseUrl = "https://huggingface.co/api";
    std::string userAgent = "FunshiEngineCLI/1.0";
    std::string lastError;

    Impl() {
        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        }
    }

    ~Impl() {
        if (curl) curl_easy_cleanup(curl);
    }

    const std::string& getLastError() const { return lastError; }
    void clearLastError() { lastError.clear(); }

    bool get(const std::string& url, std::string& response,
             const std::string& token,
             std::function<void(double)> progress,
             long timeoutMs) {
        if (!curl) return false;

        response.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L); // 10s connect timeout
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // seguro en hilos
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, progress ? 0L : 1L);
        if (progress) {
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, ("User-Agent: " + userAgent).c_str());
        if (!token.empty()) {
            headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // SSL settings for HF API
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        offline = false;
        if (res != CURLE_OK) {
            offline = res == CURLE_COULDNT_RESOLVE_HOST || res == CURLE_COULDNT_CONNECT ||
                      res == CURLE_OPERATION_TIMEDOUT || res == CURLE_COULDNT_RESOLVE_PROXY ||
                      res == CURLE_SSL_CONNECT_ERROR || res == CURLE_RECV_ERROR ||
                      res == CURLE_SEND_ERROR || res == CURLE_GOT_NOTHING;
            lastError = offline ? http::mensajeSinConexion()
                                : "cURL error: " + std::string(curl_easy_strerror(res));
            return false;
        }
        if (httpCode != 200) {
            lastError = "HTTP " + std::to_string(httpCode) + ": " + response.substr(0, 200);
            return false;
        }
        return true;
    }
    bool offline = false;

};

}  // namespace funshi::agente::llm
