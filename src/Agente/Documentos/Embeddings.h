/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#pragma once

// Embeddings para el RAG de documentos.
//
//   ProveedorEmbeddings   contrato (las pruebas usan uno falso)
//   ClienteEmbeddings     POST /v1/embeddings de un servidor OpenAI-compatible
//   ServidorEmbeddings    el servidor local: descarga Qwen3-Embedding si falta
//                         y lanza llama-server --embedding (registro propio,
//                         convive con el del chat); se detiene al salir
//
// Qwen3-Embedding pide una instrucción delante de las consultas y nada
// delante de los documentos; el cliente la agrega.

#include "Agente/LLM/Config/LlmConfig.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace funshi::agente::llm {
class LocalInferenceManager;
}

namespace funshi::agente::documentos {

class ProveedorEmbeddings {
public:
    virtual ~ProveedorEmbeddings() = default;
    virtual std::string modelo() const = 0;
    // Un vector normalizado por texto, en el mismo orden. `consulta`: los
    // textos son preguntas del usuario (no fragmentos de documentos).
    virtual bool embeber(const std::vector<std::string>& textos, bool consulta,
                         std::vector<std::vector<float>>& vectores, std::string& error) = 0;
};

class ClienteEmbeddings : public ProveedorEmbeddings {
public:
    ClienteEmbeddings(std::string baseUrl, std::string modelo, std::string apiKey = "");
    std::string modelo() const override { return modelo_; }
    bool embeber(const std::vector<std::string>& textos, bool consulta,
                 std::vector<std::vector<float>>& vectores, std::string& error) override;

    static constexpr const char* kInstruccionConsulta =
        "Instruct: Given a question, retrieve passages from the user's documents that answer it\nQuery: ";

private:
    std::string baseUrl_;
    std::string modelo_;
    std::string apiKey_;
};

// Modelo de embeddings por defecto.
inline constexpr const char* kRepoEmbeddings = "Qwen/Qwen3-Embedding-0.6B-GGUF";
inline constexpr const char* kArchivoEmbeddings = "Qwen3-Embedding-0.6B-Q8_0.gguf";

class ServidorEmbeddings {
public:
    static ServidorEmbeddings& global();
    ~ServidorEmbeddings();

    using Progreso = std::function<void(const std::string&)>;

    // Proveedor listo: con embeddingsUrl en la config usa ese servidor; si no,
    // descarga el modelo (si falta) y levanta el local. nullptr + error si no
    // se pudo (sin conexión, sin llama-server…).
    std::shared_ptr<ProveedorEmbeddings> asegurar(const llm::LlmConfig& config, const Progreso& progreso,
                                                  std::string& error);
    // El proveedor si ya está listo (sin descargar ni lanzar nada).
    std::shared_ptr<ProveedorEmbeddings> activo() const;
    void detener();

    static std::filesystem::path rutaModelo(const llm::LlmConfig& config);

private:
    ServidorEmbeddings() = default;
    mutable std::mutex mutex_;
    std::unique_ptr<llm::LocalInferenceManager> servidor_;
    std::shared_ptr<ProveedorEmbeddings> proveedor_;
};

}  // namespace funshi::agente::documentos
