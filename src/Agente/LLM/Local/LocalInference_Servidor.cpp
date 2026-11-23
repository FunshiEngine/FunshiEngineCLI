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

// LocalInferenceManager: arranque, consulta y parada de llama-server.
#include "Agente/LLM/Local/LocalInference.h"
#include "Agente/LLM/HuggingFace/HfApi.h"
#include "Agente/LLM/Cliente/HttpDescarga.h"
#include "Agente/LLM/Local/LlamaBinarios.h"
#include "Agente/LLM/Local/RegistroServidor.h"

#include <curl/curl.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

// Network/socket headers
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>

namespace funshi::agente::llm {

namespace {

// Helper para ejecutar comandos y capturar salida
struct CommandResult {
    int exitCode = -1;
    std::string stdoutOutput;
    std::string stderrOutput;
};

CommandResult runCommandCapture(const std::string& cmd, const std::filesystem::path& cwd) {
    CommandResult result;
    
    std::string fullCmd = "cd \"" + cwd.string() + "\" && " + cmd + " 2>&1";
    
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        result.exitCode = -1;
        return result;
    }
    
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.stdoutOutput += buffer;
    }
    
    result.exitCode = pclose(pipe);
    if (result.exitCode != -1) {
        result.exitCode = WEXITSTATUS(result.exitCode);
    }
    return result;
}

size_t HealthCheckWrite(void* ptr, size_t size, size_t nmemb, void* userdata) {
    return size * nmemb;
}

bool procesoVivo(int pid) {
    if (pid <= 0) return true;  // sin PID no se puede saber: se asume vivo
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
}

// Espera a que el servidor responda. Sondea cada 150 ms y termina antes si el
// proceso murió (p. ej. por falta de memoria) en lugar de agotar el timeout.
bool waitForServerHealth(const std::string& url, int pid, int timeoutMs = 120000) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, HealthCheckWrite);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    bool ok = false;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
        if (curl_easy_perform(curl) == CURLE_OK) {
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (httpCode == 200) {  // /health devuelve 503 mientras carga el modelo
                ok = true;
                break;
            }
        }
        if (!procesoVivo(pid)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    curl_easy_cleanup(curl);
    return ok;
}

} // namespace
bool LocalInferenceManager::startServer(const std::filesystem::path& modelPath,
                                        int port,
                                        int gpuLayers,
                                        int contextSize,
                                        ProgressCallback progress) {
    if (serverInfo_.isRunning) {
        if (progress) progress(1.0, "Servidor ya corriendo en puerto " + std::to_string(serverInfo_.port));
        return true;
    }
    
    if (!ensureLlamaServer(progress)) {
        return false;
    }
    
    // Verificar que el binario existe
    if (!std::filesystem::exists(llamaServerBinary_)) {
        setState(LocalInferenceState::ERROR, "llama-server no encontrado en: " + llamaServerBinary_.string());
        return false;
    }
    
    // Verificar que el modelo existe
    if (!std::filesystem::exists(modelPath)) {
        setState(LocalInferenceState::ERROR, "Modelo no encontrado: " + modelPath.string());
        return false;
    }
    
    // Un solo servidor gestionado a la vez: al cambiar de modelo se detiene el
    // anterior (antes quedaba huérfano y el nuevo arrancaba en otro puerto).
    detenerServidorRegistrado(baseDir_);

    int actualPort = port;
    if (isPortInUse(port)) {
        actualPort = findFreePort(port + 1);
    }
    
    setState(LocalInferenceState::STARTING_SERVER, 
             "Iniciando llama-server en puerto " + std::to_string(actualPort));
    
    std::ostringstream cmd;
    cmd << "\"" << llamaServerBinary_.string() << "\""
        << " -m \"" << modelPath.string() << "\""
        << " -c " << contextSize
        << " --port " << actualPort
        << " --host 127.0.0.1"
        // Un solo slot: con -np automático el contexto se reparte entre varios.
        << " -np 1"
        // Reutiliza la caché KV de prefijos (historial del chat) entre peticiones.
        << " --cache-reuse 256";
    
    if (gpuLayers >= 0) {
        cmd << " -ngl " << gpuLayers;
    } else {
        cmd << " -ngl 999";
    }
    
    // Log persistente (append) en lugar de sobrescribir
    std::filesystem::path logFile = baseDir_ / "llama-server.log";
    
    // Usar nohup + setsid para daemonizar correctamente y evitar SIGHUP al salir el shell padre
    cmd << " >> \"" << logFile.string() << "\" 2>&1 </dev/null & echo $!";
    
    std::string fullCmd = "nohup " + cmd.str();
    CommandResult result = runCommandCapture(fullCmd, baseDir_);
    
    if (result.exitCode != 0 || result.stdoutOutput.empty()) {
        setState(LocalInferenceState::ERROR, "Error lanzando llama-server: " + result.stdoutOutput);
        return false;
    }
    
    // Capturar PID del proceso en background
    int pid = -1;
    try {
        pid = std::stoi(result.stdoutOutput);
    } catch (...) {
        pid = -1;
    }
    
    if (progress) progress(0.5, "Esperando health check...");
    
    std::string healthUrl = "http://127.0.0.1:" + std::to_string(actualPort) + "/health";
    if (!waitForServerHealth(healthUrl, pid)) {
        setState(LocalInferenceState::ERROR,
                 procesoVivo(pid) ? "Timeout esperando al servidor (ver " + logFile.string() + ")"
                                  : "llama-server terminó al arrancar (ver " + logFile.string() + ")");
        stopServer();
        return false;
    }
    
    registrarServidor(baseDir_, pid, actualPort);

    serverInfo_ = LocalServerInfo{
        .baseUrl = "http://127.0.0.1:" + std::to_string(actualPort) + "/v1",
        .port = actualPort,
        .modelPath = modelPath.string(),
        .modelName = modelPath.stem().string(),
        .pid = pid,
        .isRunning = true
    };
    
    if (progress) progress(1.0, "Servidor listo en " + serverInfo_.baseUrl);
    return true;
}

LocalInferenceManager::CapacidadesServidor LocalInferenceManager::consultarCapacidades() const {
    CapacidadesServidor caps;
    if (!serverInfo_.isRunning) return caps;
    std::string cuerpo;
    const auto r = http::get("http://127.0.0.1:" + std::to_string(serverInfo_.port) + "/props",
                             cuerpo, {}, "FunshiEngineCLI/1.0", 5000);
    if (!r.ok) return caps;
    const auto j = nlohmann::json::parse(cuerpo, nullptr, false);
    if (!j.is_object()) return caps;
    caps.consultado = true;
    const auto plantilla = j.value("chat_template_caps", nlohmann::json::object());
    caps.herramientasNativas = plantilla.value("supports_tools", false) &&
                               plantilla.value("supports_tool_calls", false);
    caps.contexto = j.value("default_generation_settings", nlohmann::json::object())
                        .value("n_ctx", 0);
    return caps;
}

void LocalInferenceManager::stopServer() {
    if (!serverInfo_.isRunning) return;
    
    setState(LocalInferenceState::STOPPING, "Parando servidor...");
    
    // Usar PID capturado para matar proceso espec\u00edfico
    if (serverInfo_.pid > 0) {
        std::string killCmd = "kill " + std::to_string(serverInfo_.pid) + " 2>/dev/null || true";
        system(killCmd.c_str());
        
        // Esperar un poco y forzar si sigue vivo
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::string checkCmd = "kill -0 " + std::to_string(serverInfo_.pid) + " 2>/dev/null";
        if (system(checkCmd.c_str()) == 0) {
            std::string forceKill = "kill -9 " + std::to_string(serverInfo_.pid) + " 2>/dev/null || true";
            system(forceKill.c_str());
        }
    } else {
        // Fallback: matar por puerto
        std::string killCmd = "fuser -k " + std::to_string(serverInfo_.port) + "/tcp 2>/dev/null || true";
        system(killCmd.c_str());
    }
    
    detenerServidorRegistrado(baseDir_);
    serverInfo_.isRunning = false;
    serverInfo_.pid = -1;
    
    setState(LocalInferenceState::IDLE, "Servidor parado");
}

void LocalInferenceManager::detachServer() {
    serverInfo_.isRunning = false;
    serverInfo_.pid = -1;
}

} // namespace funshi::agente::llm
