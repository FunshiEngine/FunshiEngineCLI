/*

    FunshiEngineGL - Motor de juegos 3D con OpenGL e ImGui
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
// Prueba headless del CLI (FunshiCLI): verifica respuesta JSON, eco de id,
// apiVersion en help, version y el recorrido de proyecto sobre un proyecto
// real.
//
// El binario del CLI se resuelve junto a este ejecutable (mismo directorio de
// build), con la extension .exe en Windows. Asi el test funciona en local y en
// CI sin rutas absolutas.

#include "PruebasCli.h"

#include "TempPruebas.h"

using namespace pruebas_cli;

int main(int argc, char** argv) {
    TempPruebas::CarpetaPrueba tmpDir("cli-tests");

    if (argc < 1) {
        std::cerr << "Uso: cli-tests\n";
        return 1;
    }
    const fs::path bin = rutaBinarioCLI(argv[0]);
    // Configuración aislada: la CLI busca `.agentes/` en el cwd y sus padres,
    // y los tests no deben leer ni escribir la del usuario.
    fijarVariable("FUNSHI_CONFIG_DIR", (tmpDir.ruta() / "config").string());

    probarComandosBasicos(bin, tmpDir.ruta());
    probarRecorridoProyecto(bin, tmpDir.ruta());

    // 6. Rutas configurables, validacion de argumentos y persistencia de
    // escena, todo en un directorio de trabajo aislado. Cada solicitud se
    // ejecuta en un proceso nuevo, igual que hace la CLI real.
    EntornoAislado env{bin, tmpDir.ruta() / "aislado", tmpDir.ruta()};
    fs::create_directories(env.trabajo);
    fijarVariable("FUNSHI_CONFIG_DIR", (env.trabajo / ".agentes").string());
    probarConfiguracion(env);
    probarEscenaYArchivos(env);
    probarAgente(env);

    std::cout << (fallos == 0 ? "OK" : "FALLOS") << ": "
              << total << " comprobaciones, " << fallos << " fallos\n";
    return fallos == 0 ? 0 : 1;
}
