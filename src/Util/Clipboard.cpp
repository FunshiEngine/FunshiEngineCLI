/*
 * Copyright 2026 FunshiEngine
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
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Util/Clipboard.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace funshi::util {

namespace {

bool hayVariable(const char* nombre) {
    const char* v = std::getenv(nombre);
    return v != nullptr && *v != '\0';
}

#ifndef _WIN32
// Busca un ejecutable en PATH sin lanzar procesos.
bool enPath(const std::string& programa) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string resto = path;
    std::size_t inicio = 0;
    while (inicio <= resto.size()) {
        const std::size_t fin = resto.find(':', inicio);
        const std::string dir = resto.substr(inicio, fin == std::string::npos ? std::string::npos : fin - inicio);
        if (!dir.empty()) {
            std::error_code ec;
            const auto ruta = std::filesystem::path(dir) / programa;
            const auto st = std::filesystem::status(ruta, ec);
            if (!ec && std::filesystem::is_regular_file(st) &&
                (st.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
                return true;
            }
        }
        if (fin == std::string::npos) break;
        inicio = fin + 1;
    }
    return false;
}

struct Herramienta {
    const char* nombre;   // para informar
    const char* comando;  // lee el texto por stdin
};

// Herramientas utilizables en este entorno, en orden de preferencia.
// Se calculan una vez: el entorno no cambia durante la sesión.
const std::vector<Herramienta>& herramientas() {
    static const std::vector<Herramienta>* lista = [] {
        auto* l = new std::vector<Herramienta>();  // nunca se destruye (hilos)
#ifdef __APPLE__
        if (enPath("pbcopy")) l->push_back({"pbcopy", "pbcopy"});
#else
        if (hayVariable("WAYLAND_DISPLAY") && enPath("wl-copy")) {
            l->push_back({"wl-copy", "wl-copy"});
        }
        if (hayVariable("DISPLAY")) {
            if (enPath("xclip")) l->push_back({"xclip", "xclip -selection clipboard -in"});
            if (enPath("xsel")) l->push_back({"xsel", "xsel --clipboard --input"});
        }
#endif
        // tmux ≥ 3.2: -w además reenvía al portapapeles de la terminal.
        if (hayVariable("TMUX") && enPath("tmux")) l->push_back({"tmux", "tmux load-buffer -w -"});
        return l;
    }();
    return *lista;
}

// Envía `texto` por stdin a `comando`. La salida va a /dev/null: xclip y
// wl-copy quedan vivos en segundo plano y no deben escribir sobre la TUI.
bool ejecutarConEntrada(const std::string& comando, const std::string& texto) {
    const std::string completo = comando + " >/dev/null 2>&1";
    FILE* tubo = popen(completo.c_str(), "w");
    if (!tubo) return false;
    const std::size_t escrito = std::fwrite(texto.data(), 1, texto.size(), tubo);
    const int rc = pclose(tubo);
    return escrito == texto.size() && rc == 0;
}

std::string ejecutarYLeer(const std::string& comando) {
    std::array<char, 4096> buffer{};
    std::string resultado;
    FILE* tubo = popen((comando + " 2>/dev/null").c_str(), "r");
    if (!tubo) return resultado;
    std::size_t n;
    while ((n = std::fread(buffer.data(), 1, buffer.size(), tubo)) > 0) resultado.append(buffer.data(), n);
    pclose(tubo);
    return resultado;
}
#endif

#ifdef _WIN32
std::wstring aUtf16(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}

std::string aUtf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
#endif

std::string base64(const std::string& datos) {
    static const char* tabla = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((datos.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < datos.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(datos[i]) << 16) |
                           (static_cast<unsigned char>(datos[i + 1]) << 8) |
                           static_cast<unsigned char>(datos[i + 2]);
        out += tabla[(v >> 18) & 63];
        out += tabla[(v >> 12) & 63];
        out += tabla[(v >> 6) & 63];
        out += tabla[v & 63];
    }
    if (i < datos.size()) {
        unsigned v = static_cast<unsigned char>(datos[i]) << 16;
        if (i + 1 < datos.size()) v |= static_cast<unsigned char>(datos[i + 1]) << 8;
        out += tabla[(v >> 18) & 63];
        out += tabla[(v >> 12) & 63];
        out += (i + 1 < datos.size()) ? tabla[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

}  // namespace

ResultadoCopia Clipboard::copiar(const std::string& texto) {
    ResultadoCopia r;
#ifdef _WIN32
    r.metodo = "Win32";
    const std::wstring w = aUtf16(texto);
    if (!OpenClipboard(nullptr)) {
        r.error = "el portapapeles de Windows está ocupado";
        return r;
    }
    EmptyClipboard();
    const std::size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL memoria = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void* destino = memoria ? GlobalLock(memoria) : nullptr;
    if (!destino) {
        if (memoria) GlobalFree(memoria);
        CloseClipboard();
        r.error = "sin memoria para el portapapeles";
        return r;
    }
    std::memcpy(destino, w.c_str(), bytes);
    GlobalUnlock(memoria);
    // Si SetClipboardData funciona, el sistema pasa a ser dueño de la memoria.
    r.ok = SetClipboardData(CF_UNICODETEXT, memoria) != nullptr;
    if (!r.ok) {
        GlobalFree(memoria);
        r.error = "SetClipboardData falló";
    }
    CloseClipboard();
    return r;
#else
    const auto& lista = herramientas();
    if (lista.empty()) {
        r.error = "no hay herramienta de portapapeles (instala wl-clipboard o xclip)";
        return r;
    }
    for (const auto& h : lista) {
        r.metodo = h.nombre;
        if (ejecutarConEntrada(h.comando, texto)) {
            r.ok = true;
            r.error.clear();
            return r;
        }
        r.error = std::string(h.nombre) + " falló";
    }
    return r;
#endif
}

bool Clipboard::hayHerramientaSistema() {
#ifdef _WIN32
    return true;
#else
    return !herramientas().empty();
#endif
}

std::string Clipboard::secuenciaOsc52(const std::string& texto) {
    // Varias terminales descartan secuencias de más de ~100 KB.
    constexpr std::size_t kMaxTexto = 74 * 1024;
    if (texto.empty() || texto.size() > kMaxTexto) return {};
    const std::string osc = "\x1b]52;c;" + base64(texto) + "\x07";
    if (hayVariable("TMUX")) {
        // Passthrough de tmux: cada ESC interno se duplica.
        std::string interno;
        for (char c : osc) {
            interno += c;
            if (c == '\x1b') interno += '\x1b';
        }
        return "\x1bPtmux;" + interno + "\x1b\\";
    }
    const char* term = std::getenv("TERM");
    if (term && std::strncmp(term, "screen", 6) == 0 && !hayVariable("TMUX")) {
        return "\x1bP" + osc + "\x1b\\";
    }
    return osc;
}

std::optional<std::string> Clipboard::paste() {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return std::nullopt;
    std::string resultado;
    if (HANDLE datos = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* w = static_cast<const wchar_t*>(GlobalLock(datos))) {
            resultado = aUtf8(w);
            GlobalUnlock(datos);
        }
    }
    CloseClipboard();
    return resultado.empty() ? std::nullopt : std::optional<std::string>(std::move(resultado));
#else
    std::string resultado;
#ifdef __APPLE__
    if (enPath("pbpaste")) resultado = ejecutarYLeer("pbpaste");
#else
    if (hayVariable("WAYLAND_DISPLAY") && enPath("wl-paste")) {
        resultado = ejecutarYLeer("wl-paste --no-newline");
    } else if (hayVariable("DISPLAY") && enPath("xclip")) {
        resultado = ejecutarYLeer("xclip -selection clipboard -o");
    } else if (hayVariable("DISPLAY") && enPath("xsel")) {
        resultado = ejecutarYLeer("xsel --clipboard --output");
    }
#endif
    return resultado.empty() ? std::nullopt : std::optional<std::string>(std::move(resultado));
#endif
}

}  // namespace funshi::util
