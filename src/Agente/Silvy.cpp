/*
    FunshiEngineCLI - CLI de automación del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    SPDX-License-Identifier: Apache-2.0
*/
#include "Agente/Silvy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace funshi {

namespace {

std::string minusculas(std::string texto) {
    std::transform(texto.begin(), texto.end(), texto.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return texto;
}

std::vector<std::string> partirPorEspacios(const std::string& texto) {
    std::vector<std::string> tokens;
    std::istringstream iss(texto);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

}  // namespace

Silvy::Silvy(Harness& harness) : harness_(harness) {}

bool Silvy::esJsonValido(const std::string& texto) const {
    try {
        nlohmann::json j = nlohmann::json::parse(texto);
        return j.is_object() && j.contains("command") &&
               j["command"].is_string();
    } catch (...) {
        return false;
    }
}

OrdenProcesada Silvy::interpretarTextoPlano(const std::string& texto) {
    OrdenProcesada orden;
    orden.textoOriginal = texto;

    std::string textoLimpio = texto;
    // Quitar comillas rodeando si las hay.
    if (!textoLimpio.empty() && textoLimpio.front() == '"' &&
        textoLimpio.back() == '"') {
        textoLimpio = textoLimpio.substr(1, textoLimpio.size() - 2);
    }

    std::string bajo = minusculas(textoLimpio);
    std::vector<std::string> tokens = partirPorEspacios(bajo);

    if (tokens.empty()) {
        orden.motivoRechazo = "Orden vacía.";
        return orden;
    }

    // Palabras de ayuda.
    if (bajo == "ayuda" || bajo == "help" || bajo == "?") {
        orden.motivoRechazo =
            "Escribe una orden como: 'crear proyecto MiJuego 2d', "
            "'listar proyectos', o pega una solicitud JSON.";
        return orden;
    }

    // --- project.create: "crear proyecto <nombre> [2d|3d]" ---
    if (bajo.find("crear proyecto") == 0 ||
        bajo.find("proyecto crear") == 0 ||
        bajo.find("create project") == 0) {
        std::string resto = bajo;
        std::string prefijos[] = {"crear proyecto ", "proyecto crear ",
                                  "create project "};
        for (const std::string& p : prefijos) {
            if (resto.find(p) == 0) {
                resto = resto.substr(p.size());
                break;
            }
        }
        std::vector<std::string> partes = partirPorEspacios(resto);
        if (partes.empty()) {
            orden.motivoRechazo =
                "Falta el nombre del proyecto. Ejemplo: crear proyecto MiJuego 2d";
            return orden;
        }
        orden.solicitud.comando = "project.create";
        orden.solicitud.argumentos["name"] = partes[0];
        int dimension = 3;
        if (partes.size() > 1) {
            std::string dim = minusculas(partes[1]);
            if (dim == "2d" || dim == "2" || dim == "dosd") {
                dimension = 2;
            } else if (dim == "3d" || dim == "3" || dim == "tresd") {
                dimension = 3;
            }
        }
        orden.solicitud.argumentos["dimension"] = dimension;
        orden.interpretada = true;
        return orden;
    }

    // --- project.list: "listar proyectos" ---
    if (bajo == "listar proyectos" || bajo == "projects list" ||
        bajo == "list projects") {
        orden.solicitud.comando = "project.list";
        orden.solicitud.argumentos = nlohmann::json::object();
        orden.interpretada = true;
        return orden;
    }

    // --- diagnostics.list: "diagnósticos" / "ver diagnósticos" ---
    if (bajo.find("diagnóstico") != std::string::npos ||
        bajo.find("diagnostico") != std::string::npos) {
        orden.solicitud.comando = "diagnostics.list";
        orden.solicitud.argumentos = nlohmann::json::object();
        orden.interpretada = true;
        return orden;
    }

    // --- version: "versión" ---
    if (bajo == "versión" || bajo == "version") {
        orden.solicitud.comando = "version";
        orden.solicitud.argumentos = nlohmann::json::object();
        orden.interpretada = true;
        return orden;
    }

    // --- help: "comandos" ---
    if (bajo == "comandos" || bajo == "lista comandos") {
        orden.solicitud.comando = "help";
        orden.solicitud.argumentos = nlohmann::json::object();
        orden.interpretada = true;
        return orden;
    }

    orden.motivoRechazo =
        "No pude interpretar la orden. Usa ayuda para ver ejemplos o pega un JSON con 'command'.\n";
    return orden;
}

OrdenProcesada Silvy::interpretar(const std::string& texto) {
    OrdenProcesada orden;
    orden.textoOriginal = texto;

    if (texto.empty()) {
        orden.motivoRechazo = "Orden vacía.";
        return orden;
    }

    // Si es una solicitud JSON completa, usarla directamente.
    if (esJsonValido(texto)) {
        try {
            nlohmann::json j = nlohmann::json::parse(texto);
            orden.solicitud.comando = j["command"].get<std::string>();
            orden.solicitud.argumentos =
                j.value("args", nlohmann::json::object());
            if (j.contains("id")) {
                orden.solicitud.id = j["id"];
            }
            orden.interpretada = true;
            return orden;
        } catch (const nlohmann::json::exception& e) {
            orden.motivoRechazo = "JSON inválido: " + std::string(e.what());
            return orden;
        }
    }

    return interpretarTextoPlano(texto);
}

ResultadoComando Silvy::ejecutarOrden(const std::string& texto) {
    OrdenProcesada orden = interpretar(texto);
    historial_.push_back(orden);

    if (!orden.interpretada) {
        ResultadoComando resultado =
            ResultadoComando::fallo("orden_no_interpretada",
                                    orden.motivoRechazo,
                                    codigosSalida::kErrorUso);
        // Registrar en la auditoría como intento fallido.
        EntradaAuditoria entrada;
        entrada.marcaTiempo = harness_.auditoria().ahoraIso8601();
        entrada.comando = "<no interpretado>";
        entrada.argumentos = nlohmann::json::object();
        entrada.ok = false;
        entrada.codigoError = "orden_no_interpretada";
        entrada.mensajeError = orden.motivoRechazo;
        entrada.codigoSalida = resultado.codigoSalida;
        entrada.modificaDisco = false;
        entrada.observaciones = "textoOriginal=" + texto;
        const_cast<Auditoria&>(harness_.auditoria()).registrar(entrada);
        return resultado;
    }

    return harness_.ejecutar(orden.solicitud,
                             /*modificaDisco=*/false);
}

std::string Silvy::resumenQueHice(std::size_t n) const {
    return harness_.auditoria().resumenReciente(n);
}

}  // namespace funshi
