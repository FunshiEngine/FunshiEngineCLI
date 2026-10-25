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

#include "Agente/LLM/LocalInference.h"
#include "Agente/LLM/HfApi.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

// Network/socket headers
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>

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
    
    std::string fullCmd = "cd " + cwd.string() + " && " + cmd + " 2>&1";
    
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

bool runCommandWithProgress(const std::string& cmd, const std::filesystem::path& cwd,
                            ProgressCallback progress, const std::string& phaseName) {
    if (progress) progress(0.0, phaseName + ": iniciando...");
    
    std::string fullCmd = "cd " + cwd.string() + " && " + cmd + " 2>&1";
    
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        if (progress) progress(0.0, phaseName + ": error al lanzar");
        return false;
    }
    
    char buffer[4096];
    int lineCount = 0;
    std::string lastOutput;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        lineCount++;
        lastOutput = buffer;
        if (progress && lineCount % 10 == 0) {
            double p = std::min(0.9, lineCount * 0.01);
            progress(p, phaseName + ": " + std::string(buffer).substr(0, 80));
        }
    }
    
    int status = pclose(pipe);
    int exitCode = (status != -1) ? WEXITSTATUS(status) : -1;
    
    if (progress) {
        if (exitCode == 0) {
            progress(1.0, phaseName + ": completado");
        } else {
            progress(0.0, phaseName + ": falló (exit " + std::to_string(exitCode) + ") " + lastOutput.substr(0, 100));
        }
    }
    
    return exitCode == 0;
}

size_t HealthCheckWrite(void* ptr, size_t size, size_t nmemb, void* userdata) {
    return size * nmemb;
}

bool waitForServerHealth(const std::string& url, int timeoutMs = 30000) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, HealthCheckWrite);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            if (httpCode == 200) {
                curl_easy_cleanup(curl);
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    curl_easy_cleanup(curl);
    return false;
}

} // namespace

// ==================== LocalInferenceManager Implementation ====================

LocalInferenceManager::LocalInferenceManager(std::string baseDir) {
    if (baseDir.empty()) {
        const char* home = std::getenv("HOME");
        baseDir_ = std::filesystem::path(home ? home : ".") / ".agentes" / "local-inference";
    } else {
        baseDir_ = std::filesystem::path(baseDir);
    }
    
    llamaCppDir_ = baseDir_ / "llama.cpp";
    modelsDir_ = baseDir_ / "models";
    llamaServerBinary_ = llamaCppDir_ / "build" / "bin" / "llama-server";
    
    std::filesystem::create_directories(baseDir_);
    std::filesystem::create_directories(modelsDir_);
}

LocalInferenceManager::~LocalInferenceManager() {
    stopServer();
}

bool LocalInferenceManager::isLlamaCppReady() const {
    return std::filesystem::exists(llamaServerBinary_) && 
           std::filesystem::is_regular_file(llamaServerBinary_);
}

void LocalInferenceManager::setState(LocalInferenceState s, const std::string& msg) {
    state_ = s;
    if (!msg.empty()) lastError_ = msg;
    if (stateCb_) stateCb_(s, msg);
}

bool LocalInferenceManager::isPortInUse(int port) const {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    
    int result = connect(sock, (sockaddr*)&addr, sizeof(addr));
    close(sock);
    return result == 0;
}

int LocalInferenceManager::findFreePort(int start) const {
    for (int port = start; port < start + 100; ++port) {
        if (!isPortInUse(port)) return port;
    }
    return start;
}

bool LocalInferenceManager::ensureLlamaCppCloned(ProgressCallback progress) {
    if (std::filesystem::exists(llamaCppDir_ / ".git")) {
        if (progress) progress(1.0, "llama.cpp ya clonado");
        return true;
    }
    
    setState(LocalInferenceState::CLONING_REPO, "Clonando llama.cpp...");
    
    if (std::filesystem::exists(llamaCppDir_)) {
        std::filesystem::remove_all(llamaCppDir_);
    }
    
    std::string cmd = "git clone --depth 1 --branch master https://github.com/ggml-org/llama.cpp.git " + 
                      llamaCppDir_.string();
    
    bool ok = runCommandWithProgress(cmd, baseDir_, progress, "Clonando llama.cpp");
    
    if (!ok) {
        setState(LocalInferenceState::ERROR, "Error clonando llama.cpp");
        return false;
    }
    
    return true;
}

bool LocalInferenceManager::ensureLlamaCppBuilt(ProgressCallback progress) {
    if (isLlamaCppReady()) {
        if (progress) progress(1.0, "llama.cpp ya compilado");
        return true;
    }
    
    if (!ensureLlamaCppCloned(progress)) {
        return false;
    }
    
    setState(LocalInferenceState::BUILDING, "Compilando llama.cpp...");
    
    std::string cmakeCmd = "cmake -B build -DCMAKE_BUILD_TYPE=Release -DLLAMA_BUILD_SERVER=ON "
                          "-DLLAMA_CURL=ON -DLLAMA_CUBLAS=OFF -DLLAMA_METAL=OFF";
    
    bool ok = runCommandWithProgress(cmakeCmd, llamaCppDir_, progress, "Configurando CMake");
    if (!ok) {
        setState(LocalInferenceState::ERROR, "Error configurando CMake");
        return false;
    }
    
    std::string buildCmd = "cmake --build build --config Release --target llama-server -j" + 
                           std::to_string(std::thread::hardware_concurrency());
    
    ok = runCommandWithProgress(buildCmd, llamaCppDir_, progress, "Compilando llama-server");
    if (!ok) {
        setState(LocalInferenceState::ERROR, "Error compilando llama-server");
        return false;
    }
    
    if (!isLlamaCppReady()) {
        setState(LocalInferenceState::ERROR, "Binario llama-server no encontrado tras compilar");
        return false;
    }
    
    return true;
}

bool LocalInferenceManager::downloadModel(const ModelVariant& variant, 
                                          ProgressCallback progress) {
    setState(LocalInferenceState::DOWNLOADING_MODEL, "Descargando modelo: " + variant.filename);
    
    std::filesystem::path destPath = modelsDir_ / variant.filename;
    
    if (std::filesystem::exists(destPath) && !variant.sha256.empty()) {
        if (progress) progress(1.0, "Modelo ya existe: " + variant.filename);
        return true;
    }
    
    auto adaptedProgress = [progress](double p) {
        if (progress) progress(p, "Descargando...");
    };
    
    HfApi api(modelsDir_.string());
    bool ok = api.downloadVariant(variant, destPath.string(), adaptedProgress);
    
    if (!ok) {
        setState(LocalInferenceState::ERROR, "Error descargando modelo: " + api.getLastError());
        return false;
    }
    
    if (progress) progress(1.0, "Descarga completada: " + variant.filename);
    return true;
}

bool LocalInferenceManager::startServer(const std::filesystem::path& modelPath,
                                        int port,
                                        int gpuLayers,
                                        int contextSize,
                                        ProgressCallback progress) {
    if (serverInfo_.isRunning) {
        if (progress) progress(1.0, "Servidor ya corriendo en puerto " + std::to_string(serverInfo_.port));
        return true;
    }
    
    if (!ensureLlamaCppBuilt(progress)) {
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
    
    int actualPort = port;
    if (isPortInUse(port)) {
        actualPort = findFreePort(port + 1);
    }
    
    setState(LocalInferenceState::STARTING_SERVER, 
             "Iniciando llama-server en puerto " + std::to_string(actualPort));
    
    std::ostringstream cmd;
    cmd << llamaServerBinary_.string() 
        << " -m " << modelPath.string()
        << " -c " << contextSize
        << " --port " << actualPort
        << " --host 127.0.0.1";
    
    if (gpuLayers >= 0) {
        cmd << " -ngl " << gpuLayers;
    } else {
        cmd << " -ngl 999";
    }
    
    // Log persistente (append) en lugar de sobrescribir
    std::filesystem::path logFile = baseDir_ / "llama-server.log";
    
    // Usar nohup + setsid para daemonizar correctamente y evitar SIGHUP al salir el shell padre
    cmd << " >> " << logFile.string() << " 2>&1 </dev/null & echo $!";
    
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
    
    std::string healthUrl = "http://127.0.0.1:" + std::to_string(actualPort) + "/v1/models";
    if (!waitForServerHealth(healthUrl, 30000)) {
        setState(LocalInferenceState::ERROR, "Timeout esperando health check del servidor");
        stopServer();
        return false;
    }
    
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
    
    serverInfo_.isRunning = false;
    serverInfo_.pid = -1;
    
    setState(LocalInferenceState::IDLE, "Servidor parado");
}

} // namespace funshi::agente::llm
