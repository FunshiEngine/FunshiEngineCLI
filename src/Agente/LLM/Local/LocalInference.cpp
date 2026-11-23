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

// Construcción, estado y preparación del binario. La descarga de modelos
// está en LocalInference_Descarga.cpp y el ciclo de vida del servidor en
// LocalInference_Servidor.cpp.
// ==================== LocalInferenceManager Implementation ====================

LocalInferenceManager::LocalInferenceManager(std::string baseDir,
                                             std::string modelsDir,
                                             std::string llamaServerBinary) {
    auto resolverRuta = [](const std::string& valor, const std::filesystem::path& defecto) {
        if (valor.empty()) return defecto;
        std::filesystem::path p(valor);
        std::error_code ec;
        return p.is_absolute() ? p : std::filesystem::absolute(p, ec);
    };

    if (baseDir.empty()) {
        const char* home = std::getenv("HOME");
#ifdef _WIN32
        if (!home) home = std::getenv("USERPROFILE");
#endif
        baseDir_ = std::filesystem::path(home ? home : ".") / ".agentes" / "local-inference";
    } else {
        baseDir_ = resolverRuta(baseDir, {});
    }

    modelsDir_ = resolverRuta(modelsDir, baseDir_ / "models");
    servidorPersonalizado_ = !llamaServerBinary.empty();
    llamaServerBinary_ = servidorPersonalizado_
                             ? resolverRuta(llamaServerBinary, {})
                             : rutaLlamaServerInstalado(baseDir_);

    std::error_code ec;
    std::filesystem::create_directories(baseDir_, ec);
    ec.clear();
    std::filesystem::create_directories(modelsDir_, ec);
}

LocalInferenceManager::~LocalInferenceManager() {
    stopServer();
}

bool LocalInferenceManager::isLlamaServerReady() const {
    std::error_code ec;
    return !llamaServerBinary_.empty() && std::filesystem::is_regular_file(llamaServerBinary_, ec);
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

bool LocalInferenceManager::ensureLlamaServer(ProgressCallback progress) {
    if (servidorPersonalizado_) {
        // Binario indicado por el usuario en la configuración: se respeta.
        if (isLlamaServerReady()) {
            if (progress) progress(1.0, "Usando llama-server configurado: " + llamaServerBinary_.string());
            return true;
        }
        setState(LocalInferenceState::ERROR,
                 "llama-server configurado no existe: " + llamaServerBinary_.string());
        return false;
    }

    setState(LocalInferenceState::PREPARING_SERVER, "Comprobando llama-server...");
    InstaladorLlama instalador(baseDir_);
    const EstadoLlamaServer estado = instalador.asegurarActualizado(progress);
    if (!estado.ok) {
        setState(LocalInferenceState::ERROR, estado.mensaje);
        return false;
    }
    llamaServerBinary_ = estado.servidor;
    if (progress) progress(1.0, estado.mensaje);
    setState(LocalInferenceState::IDLE, estado.mensaje);
    return true;
}

} // namespace funshi::agente::llm
