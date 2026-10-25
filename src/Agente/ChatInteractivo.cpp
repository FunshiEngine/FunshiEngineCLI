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
#include "Agente/ChatInteractivo.h"

#include "Nucleo/CatalogoComandos.h"
#include "Nucleo/Respuesta.h"

#include <iostream>
#include <string>

namespace funshi {

namespace {

void mostrarAyudaChat() {
    std::cout << "=== Chat interactivo de Silvy ===\n"
              << "Escribe una orden y pulsa Enter. Ejemplos:\n"
              << "  crear proyecto MiJuego 2d\n"
              << "  listar proyectos\n"
              << "  diagnosticos\n"
              << "  versión\n"
              << "  comandos\n"
              << "También puedes pegar una solicitud JSON completa:\n"
              << "  {\"command\":\"project.create\",\"args\":{\"name\":\"X\",\"dimension\":2}}\n"
              << "\nComandos del chat:\n"
              << "  ayuda / help   - muestra esta ayuda\n"
              << "  historial      - últimas ejecuciones de Silvy\n"
              << "  cambios        - solo operaciones que modificaron disco\n"
              << "  salir / exit   - termina el chat\n"
              << std::endl;
}

void mostrarRespuesta(const ResultadoComando& resultado) {
    const Respuesta& r = resultado.respuesta;
    std::cout << r.aTexto() << '\n';
    if (!r.correcta) {
        std::cout << "(código de salida " << resultado.codigoSalida << ")\n";
    }
}

}  // namespace

int ejecutarChatSilvy(Silvy& silvy) {
    std::cout << "=== Silvy - Agente de FunshiEngineCLI ===\n"
              << "Escribe 'ayuda' para ver ejemplos o 'salir' para terminar.\n"
              << std::endl;

    while (true) {
        std::cout << "silvy> ";
        std::cout.flush();

        std::string linea;
        if (!std::getline(std::cin, linea)) {
            // EOF (Ctrl+D) o error de entrada.
            std::cout << "\n";
            break;
        }

        // Quitar espacios finales.
        std::string recortada = linea;
        while (!recortada.empty() &&
               (recortada.back() == '\r' || recortada.back() == ' ' ||
                recortada.back() == '\t')) {
            recortada.pop_back();
        }

        std::string bajo = recortada;
        std::transform(bajo.begin(), bajo.end(), bajo.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (bajo == "salir" || bajo == "exit" || bajo == "quit") {
            std::cout << "Chat finalizado.\n";
            return 0;
        }

        if (bajo == "ayuda" || bajo == "help" || bajo == "?") {
            mostrarAyudaChat();
            continue;
        }

        if (bajo == "historial") {
            std::cout << silvy.resumenQueHice(15) << std::endl;
            continue;
        }

        if (bajo == "cambios") {
            std::cout << "Operaciones que modificaron disco:\n";
            auto cambios = silvy.harness().auditoria().cambiosEnDisco();
            if (cambios.empty()) {
                std::cout << "  (ninguna)\n";
            } else {
                for (const EntradaAuditoria& e : cambios) {
                    std::cout << "  [" << e.marcaTiempo << "] " << e.comando
                              << " -> " << (e.ok ? "ok" : "error")
                              << "\n";
                }
            }
            std::cout << std::endl;
            continue;
        }

        // Ejecutar la orden con Silvy.
        ResultadoComando resultado = silvy.ejecutarOrden(recortada);
        mostrarRespuesta(resultado);
    }

    return 0;
}

}  // namespace funshi
