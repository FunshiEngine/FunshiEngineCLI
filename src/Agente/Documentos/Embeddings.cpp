/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Documentos/Embeddings.h"

#include "Agente/Documentos/AlmacenVectores.h"
#include "Agente/LLM/Cliente/HttpDescarga.h"
#include "Agente/LLM/HuggingFace/HfApi.h"
#include "Agente/LLM/Local/LocalInference.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace funshi::agente::documentos {

namespace fs = std::filesystem;

namespace {

constexpr int kPuertoEmbeddings = 11500;
constexpr int kContextoEmbeddings = 4096;

std::size_t escribir(char* datos, std::size_t tam, std::size_t n, void* destino) {
    static_cast<std::string*>(destino)->append(datos, tam * n);
    return tam * n;
}

}  // namespace

ClienteEmbeddings::ClienteEmbeddings(std::string baseUrl, std::string modelo, std::string apiKey)
    : baseUrl_(std::move(baseUrl)), modelo_(std::move(modelo)), apiKey_(std::move(apiKey)) {
    while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
}

bool ClienteEmbeddings::embeber(const std::vector<std::string>& textos, bool consulta,
                                std::vector<std::vector<float>>& vectores, std::string& error) {
    vectores.clear();
    if (textos.empty()) return true;
    nlohmann::json entrada = nlohmann::json::array();
    for (const auto& t : textos) entrada.push_back(consulta ? std::string(kInstruccionConsulta) + t : t);
    const std::string cuerpo = nlohmann::json{{"model", modelo_}, {"input", entrada}}.dump();

    llm::http::inicializarCurl();
    CURL* curl = curl_easy_init();  // un handle por llamada: seguro entre hilos
    if (!curl) { error = "no se pudo iniciar cURL"; return false; }
    std::string respuesta;
    curl_slist* cabeceras = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!apiKey_.empty()) cabeceras = curl_slist_append(cabeceras, ("Authorization: Bearer " + apiKey_).c_str());
    const std::string url = baseUrl_ + "/embeddings";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cabeceras);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, cuerpo.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(cuerpo.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, escribir);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respuesta);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 300000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    const CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(cabeceras);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { error = std::string("embeddings: ") + curl_easy_strerror(res); return false; }
    const auto j = nlohmann::json::parse(respuesta, nullptr, false);
    if (http != 200 || !j.is_object() || !j.contains("data") || !j["data"].is_array()) {
        error = "embeddings: respuesta inválida (HTTP " + std::to_string(http) + "): " + respuesta.substr(0, 200);
        return false;
    }
    vectores.assign(textos.size(), {});
    for (const auto& d : j["data"]) {
        const std::size_t i = d.value("index", static_cast<std::size_t>(0));
        if (i >= vectores.size() || !d.contains("embedding") || !d["embedding"].is_array()) continue;
        for (const auto& x : d["embedding"]) vectores[i].push_back(x.get<float>());
        normalizar(vectores[i]);
    }
    if (std::any_of(vectores.begin(), vectores.end(), [](const auto& v) { return v.empty(); })) {
        error = "embeddings: faltan vectores en la respuesta";
        return false;
    }
    return true;
}

ServidorEmbeddings& ServidorEmbeddings::global() {
    static ServidorEmbeddings s;
    return s;
}

ServidorEmbeddings::~ServidorEmbeddings() { detener(); }

fs::path ServidorEmbeddings::rutaModelo(const llm::LlmConfig& config) {
    if (!config.embeddingsModelo.empty()) return config.embeddingsModelo;
    return config.modelsDirEfectivo() / kArchivoEmbeddings;
}

std::shared_ptr<ProveedorEmbeddings> ServidorEmbeddings::activo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return proveedor_;
}

std::shared_ptr<ProveedorEmbeddings> ServidorEmbeddings::asegurar(const llm::LlmConfig& config,
                                                                  const Progreso& progreso, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (proveedor_) return proveedor_;
    const auto avisar = [&](const std::string& s) { if (progreso) progreso(s); };

    if (!config.embeddingsUrl.empty()) {
        proveedor_ = std::make_shared<ClienteEmbeddings>(config.embeddingsUrl,
                                                         fs::path(rutaModelo(config)).stem().string(), config.apiKey);
        return proveedor_;
    }

    const fs::path modelo = rutaModelo(config);
    std::error_code ec;
    if (!fs::is_regular_file(modelo, ec)) {
        avisar("descargando el modelo de embeddings (" + std::string(kArchivoEmbeddings) + ")…");
        llm::HfApi api(config.hfCacheDirEfectivo().string());
        if (!config.hfToken.empty()) api.setToken(config.hfToken);
        const auto variantes = api.getVariants(kRepoEmbeddings);
        const auto it = std::find_if(variantes.begin(), variantes.end(),
                                     [](const llm::ModelVariant& v) { return v.filename == kArchivoEmbeddings; });
        if (it == variantes.end()) {
            error = api.lastErrorWasOffline() ? "sin conexión para descargar el modelo de embeddings"
                                              : "no se encontró " + std::string(kArchivoEmbeddings) + " en " + kRepoEmbeddings;
            return nullptr;
        }
        fs::create_directories(modelo.parent_path(), ec);
        int ultimo = -1;
        const bool ok = api.downloadVariant(*it, modelo.string(), [&](double, std::uint64_t recibidos, std::uint64_t total) {
            // El total puede no conocerse al principio: entonces, solo los bytes.
            const int pct = total > 0 ? static_cast<int>(recibidos * 100 / total) : -1;
            if (pct < 0 || pct / 5 != ultimo / 5) {
                if (pct >= 0) ultimo = pct;
                avisar("descargando el modelo de embeddings: " +
                       (pct >= 0 ? std::to_string(pct) + " % de " + llm::http::formatearBytes(total)
                                 : llm::http::formatearBytes(recibidos)));
            }
        });
        if (!ok) {
            error = "no se pudo descargar el modelo de embeddings: " + api.getLastError();
            return nullptr;
        }
    }

    avisar("iniciando el servidor de embeddings…");
    auto servidor = std::make_unique<llm::LocalInferenceManager>(
        config.localInferenceDirEfectivo().string(), config.modelsDirEfectivo().string(),
        config.llamaServerPath.empty() ? std::string() : config.llamaServerPathEfectivo().string());
    servidor->setModoEmbeddings(true);
    if (!servidor->startServer(modelo, kPuertoEmbeddings, -1, kContextoEmbeddings)) {
        error = "no arrancó el servidor de embeddings: " + servidor->lastError();
        return nullptr;
    }
    proveedor_ = std::make_shared<ClienteEmbeddings>(servidor->getServerInfo().baseUrl, modelo.stem().string());
    servidor_ = std::move(servidor);
    return proveedor_;
}

void ServidorEmbeddings::detener() {
    std::lock_guard<std::mutex> lock(mutex_);
    proveedor_.reset();
    if (servidor_) servidor_->stopServer();
    servidor_.reset();
}

}  // namespace funshi::agente::documentos
