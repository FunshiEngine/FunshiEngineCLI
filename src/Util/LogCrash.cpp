/*
    FunshiEngineCLI - CLI de automatización del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique
    SPDX-License-Identifier: Apache-2.0
*/
#include "Util/LogCrash.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

namespace funshi::util {

namespace {

std::atomic<bool> g_instalado{false};
std::atomic<bool> g_enCrash{false};
std::mutex g_mutex;
std::string g_rutaLog;
std::string g_ultimoLog;
std::vector<std::string> g_anotaciones;

std::string timestampAhora() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

std::string rutaPorDefecto() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec) / ".agentes" / "logs";
    std::filesystem::create_directories(dir, ec);
    return (dir / ("crash-" + timestampAhora() + ".log")).string();
}

void escribirCabecera(int fd, const char* motivo) {
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    auto w = [fd](const char* s) {
        if (!s) return;
        const size_t n = std::strlen(s);
#if defined(_WIN32)
        DWORD written = 0; HANDLE h = (HANDLE)_get_osfhandle(fd);
        if (h != INVALID_HANDLE_VALUE) WriteFile(h, s, (DWORD)n, &written, nullptr);
#else
        ::write(fd, s, n);
#endif
    };
    w("=== FunshiEngineCLI crash ===\n");
    w("motivo: "); w(motivo ? motivo : "desconocido"); w("\n");
    const char* ver = 
#ifdef FUNSHI_VERSION
        FUNSHI_VERSION;
#else
        "desconocida";
#endif
    w("version: "); w(ver); w("\n");
#else
    (void)fd; (void)motivo;
#endif
}

void escribirBacktrace(int fd) {
#if defined(__linux__) || defined(__APPLE__)
    void* frames[64];
    int n = ::backtrace(frames, 64);
    const char msg[] = "\nbacktrace:\n";
    ::write(fd, msg, sizeof(msg) - 1);
    ::backtrace_symbols_fd(frames, n, fd);
#elif defined(_WIN32)
    // Best-effort; no async-signal-safe, but we're crashing anyway.
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    void* stack[64];
    unsigned short frames = CaptureStackBackTrace(0, 64, stack, NULL);
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)std::calloc(sizeof(SYMBOL_INFO) + 256, 1);
    if (symbol) {
        symbol->MaxNameLen = 255;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        for (unsigned short i = 0; i < frames; ++i) {
            SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
            char line[512];
            int len = std::snprintf(line, sizeof(line), "  [%u] %s - 0x%0llX\n", i, symbol->Name,
                                    (unsigned long long)symbol->Address);
            DWORD written = 0; HANDLE h = (HANDLE)_get_osfhandle(fd);
            if (h != INVALID_HANDLE_VALUE && len > 0) WriteFile(h, line, (DWORD)len, &written, nullptr);
        }
        std::free(symbol);
    }
#else
    (void)fd;
#endif
}

void volcarAnotaciones(const std::string& ruta) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_anotaciones.empty()) return;
    std::ofstream out(ruta, std::ios::app);
    if (!out) return;
    out << "\nanotaciones:\n";
    for (const auto& a : g_anotaciones) out << "- " << a << "\n";
}

void manejarCrash(const char* motivo, int sig) {
    bool esperado = false;
    if (!g_enCrash.compare_exchange_strong(esperado, true)) {
        // Crash recursivo: salir rápido.
#if defined(_WIN32)
        TerminateProcess(GetCurrentProcess(), 1);
#else
        _Exit(128 + (sig > 0 ? sig : 1));
#endif
    }

    std::string ruta;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ruta = g_rutaLog.empty() ? rutaPorDefecto() : g_rutaLog;
        g_ultimoLog = ruta;
    }

#if defined(_WIN32)
    int fd = _open(ruta.c_str(), _O_CREAT | _O_WRONLY | _O_TRUNC, 0644);
#else
    int fd = ::open(ruta.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
#endif
    if (fd >= 0) {
        escribirCabecera(fd, motivo);
        escribirBacktrace(fd);
#if defined(_WIN32)
        _close(fd);
#else
        ::close(fd);
#endif
    }
    volcarAnotaciones(ruta);

    // Restaurar handler por defecto y reelevar para código de salida/shell correcto.
#if !defined(_WIN32)
    if (sig > 0) {
        std::signal(sig, SIG_DFL);
        ::raise(sig);
    }
#endif
    std::_Exit(128 + (sig > 0 ? sig : 1));
}

void manejadorSenal(int sig) {
    const char* motivo = "signal";
    switch (sig) {
        case SIGSEGV: motivo = "SIGSEGV"; break;
        case SIGABRT: motivo = "SIGABRT"; break;
        case SIGFPE:  motivo = "SIGFPE";  break;
        case SIGILL:  motivo = "SIGILL";  break;
#ifdef SIGBUS
        case SIGBUS:  motivo = "SIGBUS";  break;
#endif
        default: break;
    }
    manejarCrash(motivo, sig);
}

void manejadorTerminate() {
    const char* motivo = "std::terminate";
    try {
        auto ep = std::current_exception();
        if (ep) std::rethrow_exception(ep);
    } catch (const std::exception& e) {
        anotarCrash(std::string("exception: ") + e.what());
    } catch (...) {
        anotarCrash("exception: desconocida");
    }
    manejarCrash(motivo, 0);
}

}  // namespace

void instalarManejadorCrash(const std::string& rutaLog) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!rutaLog.empty()) g_rutaLog = rutaLog;
        else if (g_rutaLog.empty()) g_rutaLog = rutaPorDefecto();
    }
    bool esperado = false;
    if (!g_instalado.compare_exchange_strong(esperado, true)) return;

    std::signal(SIGSEGV, manejadorSenal);
    std::signal(SIGABRT, manejadorSenal);
    std::signal(SIGFPE, manejadorSenal);
    std::signal(SIGILL, manejadorSenal);
#ifdef SIGBUS
    std::signal(SIGBUS, manejadorSenal);
#endif
    std::set_terminate(manejadorTerminate);
}

std::string rutaUltimoLogCrash() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_ultimoLog;
}

void anotarCrash(const std::string& mensaje) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_anotaciones.size() < 200) g_anotaciones.push_back(mensaje);
}

}  // namespace funshi::util
