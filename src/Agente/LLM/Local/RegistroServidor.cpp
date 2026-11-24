/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/LLM/Local/RegistroServidor.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#endif

namespace funshi::agente::llm {

namespace {

// PID de cada servidor de este proceso (chat y embeddings), leídos desde el
// manejador de señales (solo operaciones async-signal-safe: atomic lock-free + kill).
std::atomic<int> g_pidServidor[2] = {{-1}, {-1}};

std::atomic<int>& ranura(const std::string& nombre) {
    return g_pidServidor[nombre == kRegistroEmbeddings ? 1 : 0];
}

std::filesystem::path rutaRegistro(const std::filesystem::path& baseDir, const std::string& nombre) {
    return baseDir / (nombre + ".pid");
}

struct Registro {
    int pid = -1;
    int puerto = 0;
    int propietario = -1;
};

bool leerRegistro(const std::filesystem::path& baseDir, const std::string& nombre, Registro& r) {
    std::ifstream in(rutaRegistro(baseDir, nombre));
    if (!in) return false;
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) return false;
    r.pid = j.value("pid", -1);
    r.puerto = j.value("puerto", 0);
    r.propietario = j.value("propietario", -1);
    return r.pid > 0;
}

void borrarRegistro(const std::filesystem::path& baseDir, const std::string& nombre) {
    std::error_code ec;
    std::filesystem::remove(rutaRegistro(baseDir, nombre), ec);
}

int pidPropio() {
#ifdef _WIN32
    return -1;
#else
    return static_cast<int>(::getpid());
#endif
}

bool vivo(int pid) {
#ifdef _WIN32
    (void)pid;
    return false;  // TODO(FW): OpenProcess/GetExitCodeProcess
#else
    return pid > 0 && ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

#ifdef __linux__
// Ruta del ejecutable de un proceso; "" si no se puede leer (otro usuario).
std::string ejecutableDe(int pid) {
    std::error_code ec;
    auto destino = std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", ec);
    if (ec) return {};
    std::string s = destino.string();
    // Tras actualizar el binario, el viejo figura como "… (deleted)".
    const std::string sufijo = " (deleted)";
    if (s.size() > sufijo.size() && s.compare(s.size() - sufijo.size(), sufijo.size(), sufijo) == 0) {
        s.resize(s.size() - sufijo.size());
    }
    return s;
}

// Evita matar un PID reciclado por otro programa.
bool esLlamaServer(int pid) {
    const std::string exe = ejecutableDe(pid);
    return exe.empty() || std::filesystem::path(exe).filename().string().rfind("llama-server", 0) == 0;
}
#else
bool esLlamaServer(int) { return true; }
#endif

void terminar(int pid) {
#ifdef _WIN32
    (void)pid;  // TODO(FW): TerminateProcess
#else
    if (!vivo(pid) || !esLlamaServer(pid)) return;
    ::kill(static_cast<pid_t>(pid), SIGTERM);
    for (int i = 0; i < 30 && vivo(pid); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (vivo(pid)) ::kill(static_cast<pid_t>(pid), SIGKILL);
#endif
}

#ifndef _WIN32
extern "C" void manejadorSenal(int senal) {
    for (auto& ranuraPid : g_pidServidor) {
        const int pid = ranuraPid.load();
        if (pid > 0) ::kill(static_cast<pid_t>(pid), SIGTERM);
    }
    std::signal(senal, SIG_DFL);
    std::raise(senal);
}
#endif

}  // namespace

void registrarServidor(const std::filesystem::path& baseDir, int pid, int puerto, const std::string& nombre) {
    if (pid <= 0) return;
    ranura(nombre).store(pid);
    nlohmann::json j = {{"pid", pid}, {"puerto", puerto}, {"propietario", pidPropio()}};
    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec);
    const auto ruta = rutaRegistro(baseDir, nombre);
    const auto tmp = ruta.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << j.dump();
    }
    std::filesystem::rename(tmp, ruta, ec);
}

void detenerServidorRegistrado(const std::filesystem::path& baseDir, const std::string& nombre) {
    Registro r;
    if (leerRegistro(baseDir, nombre, r)) {
        terminar(r.pid);
        if (ranura(nombre).load() == r.pid) ranura(nombre).store(-1);
    }
    borrarRegistro(baseDir, nombre);
}

void limpiarServidoresHuerfanos(const std::filesystem::path& baseDir,
                                const std::filesystem::path& directorioBinarios) {
    // Servidores registrados: los de una CLI viva se respetan; los de una
    // sesión cerrada se detienen. Si hay otra CLI viva no se barre /proc:
    // podría estar arrancando un servidor que todavía no registró.
    bool otraCliViva = false;
    for (const char* nombre : {kRegistroChat, kRegistroEmbeddings}) {
        Registro r;
        if (!leerRegistro(baseDir, nombre, r)) continue;
        if (r.propietario != pidPropio() && vivo(r.propietario)) {
            otraCliViva = true;
            continue;
        }
        terminar(r.pid);
        borrarRegistro(baseDir, nombre);
    }
    if (otraCliViva) return;
#ifdef __linux__
    // Servidores sin registro (versiones anteriores de la CLI o caídas):
    // cualquier llama-server cuyo ejecutable esté en los binarios gestionados.
    std::error_code ec;
    const auto base = std::filesystem::weakly_canonical(directorioBinarios, ec).string();
    if (ec || base.empty()) return;
    for (const auto& ent : std::filesystem::directory_iterator("/proc", ec)) {
        const std::string nombre = ent.path().filename().string();
        if (nombre.empty() || nombre.find_first_not_of("0123456789") != std::string::npos) continue;
        const int pid = std::stoi(nombre);
        if (pid == pidPropio()) continue;
        const std::string exe = ejecutableDe(pid);
        if (exe.empty() || exe.rfind(base + "/", 0) != 0) continue;
        if (std::filesystem::path(exe).filename().string().rfind("llama-server", 0) != 0) continue;
        terminar(pid);
    }
#else
    (void)directorioBinarios;
#endif
}

void instalarCierreServidorPorSenal() {
#ifndef _WIN32
    static std::atomic<bool> instalado{false};
    if (instalado.exchange(true)) return;
    for (int s : {SIGTERM, SIGHUP, SIGINT, SIGQUIT}) std::signal(s, manejadorSenal);
#endif
}

}  // namespace funshi::agente::llm
