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

#include "Agente/LLM/HuggingFace/HfTypes.h"
#include "Agente/LLM/Local/ArchivoModeloGguf.h"
#include <string>
#include <filesystem>
#include <functional>
#include <optional>
#include <memory>

namespace funshi::agente::llm {

// Estado de la inferencia local
enum class LocalInferenceState {
    IDLE,           // Sin hacer nada
    PREPARING_SERVER, // Descargando/actualizando el binario llama-server
    DOWNLOADING_MODEL, // Descargando GGUF
    STARTING_SERVER,   // Levantando llama-server
    RUNNING,        // Servidor corriendo
    STOPPING,       // Parando servidor
    ERROR           // Error
};

// Info del servidor local corriendo
struct LocalServerInfo {
    std::string baseUrl;       // ej. http://localhost:11434/v1
    int port = 11434;
    std::string modelPath;     // Ruta al GGUF
    std::string modelName;     // Nombre del modelo
    pid_t pid = -1;            // PID del proceso llama-server
    bool isRunning = false;
};

// Callback de progreso unificado
using ProgressCallback = std::function<void(double progress, const std::string& status)>;

// Gestor completo de inferencia local (llama.cpp + llama-server)
class LocalInferenceManager {
public:
    // `llamaServerBinary` vacío = usar el binario oficial administrado
    // (descargado en <baseDir>/llama-bin/).
    explicit LocalInferenceManager(std::string baseDir = "",
                                   std::string modelsDir = "",
                                   std::string llamaServerBinary = "");
    ~LocalInferenceManager();

    // Directorio base donde se guarda todo (.agentes/local-inference/)
    const std::filesystem::path& baseDir() const { return baseDir_; }
    const std::filesystem::path& modelsDir() const { return modelsDir_; }
    const std::filesystem::path& llamaServerBinary() const { return llamaServerBinary_; }

    // Hay un binario llama-server utilizable.
    bool isLlamaServerReady() const;

    // Asegura llama-server: descarga el binario oficial para este SO o lo
    // actualiza si hay una versión más nueva. Sin conexión usa el instalado.
    bool ensureLlamaServer(ProgressCallback progress = nullptr);

    // Descarga un modelo GGUF desde HF (reutiliza el existente si está completo)
    bool downloadModel(const ModelVariant& variant, 
                       ProgressCallback progress = nullptr);

    // Inicia llama-server con el modelo especificado
    bool startServer(const std::filesystem::path& modelPath,
                     int port = 11434,
                     int gpuLayers = -1,
                     int contextSize = 32768,
                     ProgressCallback progress = nullptr);

    // Para el servidor si está corriendo
    void stopServer();

    // Libera la propiedad del proceso del servidor para que el destructor no lo detenga.
    void detachServer();

    // Obtiene info del servidor actual
    LocalServerInfo getServerInfo() const { return serverInfo_; }

    // Capacidades de la plantilla de chat del modelo cargado (GET /props).
    // `herramientasNativas` = la plantilla acepta `tools` de OpenAI; si es
    // false, llama-server las descarta en silencio y hay que usar el modo
    // prompt. `contexto` = n_ctx real del servidor.
    struct CapacidadesServidor {
        bool consultado = false;
        bool herramientasNativas = false;
        int contexto = 0;
    };
    CapacidadesServidor consultarCapacidades() const;

    // Estado actual
    LocalInferenceState state() const { return state_; }
    std::string lastError() const { return lastError_; }

    // Configura callback de estado para UI
    void setStateCallback(std::function<void(LocalInferenceState, const std::string&)> cb) {
        stateCb_ = std::move(cb);
    }

private:
    void setState(LocalInferenceState s, const std::string& msg = "");

    std::filesystem::path baseDir_;
    std::filesystem::path modelsDir_;
    std::filesystem::path llamaServerBinary_;
    bool servidorPersonalizado_ = false;

    LocalServerInfo serverInfo_;
    LocalInferenceState state_ = LocalInferenceState::IDLE;
    std::string lastError_;
    std::function<void(LocalInferenceState, const std::string&)> stateCb_;

    // Helpers
    bool isPortInUse(int port) const;
    int findFreePort(int start = 11434) const;
};


}  // namespace funshi::agente::llm
