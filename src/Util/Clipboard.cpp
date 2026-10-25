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

#include <cstdlib>
#include <string>
#include <array>
#include <memory>
#include <cstdio>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace funshi::util {

namespace {

// Detecta si estamos en Wayland
bool isWayland() {
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    return waylandDisplay != nullptr && *waylandDisplay != '\0';
}

// Ejecuta comando y captura stdout
std::string execCommand(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Verifica si un comando existe en PATH
bool commandExists(const std::string& cmd) {
    std::string whichCmd = "which " + cmd + " 2>/dev/null";
    std::string result = execCommand(whichCmd);
    return !result.empty();
}

}  // namespace

bool Clipboard::copy(const std::string& text) {
#ifdef _WIN32
    // Windows: Win32 API
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();

    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!hGlobal) {
        CloseClipboard();
        return false;
    }

    char* pGlobal = static_cast<char*>(GlobalLock(hGlobal));
    if (pGlobal) {
        std::memcpy(pGlobal, text.c_str(), text.size() + 1);
        GlobalUnlock(hGlobal);
        SetClipboardData(CF_TEXT, hGlobal);
    } else {
        GlobalFree(hGlobal);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;

#else
    // Linux: xclip (X11) o wl-copy (Wayland)
    if (isWayland()) {
        if (commandExists("wl-copy")) {
            // wl-copy lee de stdin
            std::string cmd = "wl-copy";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (pipe) {
                fwrite(text.c_str(), 1, text.size(), pipe);
                int ret = pclose(pipe);
                return ret == 0;
            }
        }
    } else {
        // X11: xclip o xsel
        if (commandExists("xclip")) {
            // xclip -selection clipboard
            std::string cmd = "xclip -selection clipboard";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (pipe) {
                fwrite(text.c_str(), 1, text.size(), pipe);
                int ret = pclose(pipe);
                return ret == 0;
            }
        } else if (commandExists("xsel")) {
            // xsel --clipboard --input
            std::string cmd = "xsel --clipboard --input";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (pipe) {
                fwrite(text.c_str(), 1, text.size(), pipe);
                int ret = pclose(pipe);
                return ret == 0;
            }
        }
    }
    return false;
#endif
}

std::optional<std::string> Clipboard::paste() {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return std::nullopt;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) {
        CloseClipboard();
        return std::nullopt;
    }
    char* pszText = static_cast<char*>(GlobalLock(hData));
    std::string result;
    if (pszText) {
        result = pszText;
        GlobalUnlock(hData);
    }
    CloseClipboard();
    return result.empty() ? std::nullopt : std::optional<std::string>(std::move(result));
#else
    if (isWayland()) {
        if (commandExists("wl-paste")) {
            std::string result = execCommand("wl-paste 2>/dev/null");
            return result.empty() ? std::nullopt : std::optional<std::string>(std::move(result));
        }
    } else {
        if (commandExists("xclip")) {
            std::string result = execCommand("xclip -selection clipboard -o 2>/dev/null");
            return result.empty() ? std::nullopt : std::optional<std::string>(std::move(result));
        } else if (commandExists("xsel")) {
            std::string result = execCommand("xsel --clipboard --output 2>/dev/null");
            return result.empty() ? std::nullopt : std::optional<std::string>(std::move(result));
        }
    }
    return std::nullopt;
#endif
}

bool Clipboard::isAvailable() {
#ifdef _WIN32
    return true;  // Win32 API siempre disponible en Windows
#else
    if (isWayland()) {
        return commandExists("wl-copy") && commandExists("wl-paste");
    } else {
        return commandExists("xclip") || commandExists("xsel");
    }
#endif
}

}  // namespace funshi::util