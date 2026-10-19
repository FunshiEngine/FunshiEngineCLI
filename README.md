# FunshiEngineCLI

Interfaz de línea de comandos del motor [FunshiEngineGL](https://github.com/FunshiEngine/FunshiEngineGL).

Ejecuta operaciones del motor mediante solicitudes JSON sin abrir el editor
gráfico: creación y gestión de proyectos, escenas y objetos, importación de
assets, compilación de scripts y consulta del registro de diagnósticos.

El código está modularizado en un núcleo reutilizable agnóstico del
transporte (`src/Nucleo`), preparado para que una futura app de escritorio
reutilice la misma lógica sin pasar por stdin/stdout.

## Compilación

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

El ejecutable queda en `build/FunshiCLI` (Linux) o `build/FunshiCLI.exe`
(Windows).

En Windows, `FunshiCLI.exe` al arrancar carga DLLs de runtime que no se copian
a `build/`: las del toolchain MinGW-w64 (`libstdc++-6`, `libgcc_s_seh-1`,
`libwinpthread-1`) y las del motor (`libassimp-6`, `glfw3`, las de Bullet),
que en una instalación MSYS2 viven en `C:\msys64\mingw64\bin`. Ese directorio
tiene que estar en el PATH; la terminal **MINGW64** de MSYS2 ya lo trae, y en
**UCRT64** o **PowerShell** se agrega antes de ejecutar:

```bash
export PATH=/mingw64/bin:$PATH          # MSYS2 (UCRT64, MSYS, …)
```

```powershell
$env:Path += ";C:\msys64\mingw64\bin"  # PowerShell
```

Sin esas DLLs el proceso termina en el arranque sin imprimir nada (en shells
MSYS2 se ve `error while loading shared libraries: libassimp-6.dll: ...`) y
el prompt vuelve de inmediato.

## Uso rápido

```bash
FunshiCLI help
FunshiCLI version
FunshiCLI --tui
FunshiCLI --request < solicitud.json
FunshiCLI --request-file solicitud.json
```

`--tui` abre una interfaz interactiva de terminal (proyectos, escena, scripts
y diagnósticos) que no admite solicitud adjunta. Necesita una terminal
interactiva: sin ella responde `tui_terminal_unavailable` y sale con código 2
(ver «Interfaz TUI» del manual para los detalles por plataforma).

Ejemplo — crear un proyecto 2D:

```bash
echo '{"command":"project.create","args":{"name":"MiJuego2D","dimension":2}}' \
  | FunshiCLI --request
```

Respuesta:

```json
{"apiVersion":1,"error":null,"ok":true,"result":{"dimension":2,"name":"MiJuego2D","path":".../MotorGrafico/Proyects/MiJuego2D"}}
```

## Documentación

- [MANUAL_DE_USO.md](MANUAL_DE_USO.md) — referencia completa de comandos,
  argumentos, formatos de respuesta y códigos de salida.
- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) — arquitectura por capas y
  relación con el motor.

## Tests

```bash
cd build && ctest -R cli-tests --output-on-failure
```

## Licencia

Apache License 2.0 — ver [LICENSE](LICENSE).
