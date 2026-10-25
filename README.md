# FunshiEngineCLI

Interfaz de línea de comandos del motor [FunshiEngineGL](https://github.com/FunshiEngine/FunshiEngineGL).

Ejecuta operaciones del motor mediante solicitudes JSON sin abrir el editor
gráfico: creación y gestión de proyectos, escenas y objetos, importación de
assets, compilación de scripts y consulta del registro de diagnósticos.

**Novedades:**
- **Agente Silvy integrado**: lenguaje natural + comandos CLI unificados (`/comando` = CLI, texto = LLM)
- **Model Browser nativo (Hugging Face)**: búsqueda, variantes (GB, quantization), descarga automática
- **Inferencia local**: clona/compila `llama.cpp`, descarga GGUF, levanta `llama-server` — todo automático
- **Chat interactivo en TUI**: 6ª pestaña "Modelos HF" + pestaña "Chat Silvy" con indicador de modo

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

El motor se compila desde el submódulo `extern/FunshiEngine` (el CMake lo
incluye con `add_subdirectory`), así que las cabeceras con las que compila la
CLI y la biblioteca que enlaza salen siempre del mismo commit: no hace falta
un build aparte del motor.

En Windows, el build copia junto al ejecutable las DLLs de runtime que necesita
al arrancar: las del toolchain MinGW-w64 (`libstdc++-6`, `libgcc_s_seh-1`,
`libwinpthread-1`), las del motor (`libassimp-6`, `glfw3`, las de Bullet) y las
dependencias de assimp (`libminizip-1`, `zlib1`, `libbz2-1`), tomadas del
directorio del compilador (en MSYS2, `C:\msys64\mingw64\bin`). Así
`build/FunshiCLI.exe` funciona desde cualquier terminal —MINGW64, UCRT64,
PowerShell o cmd— sin preparar el PATH.

Si copiás el ejecutable a otra carpeta, llevate también esas DLLs: sin ellas
el proceso termina en el arranque sin imprimir nada (en shells MSYS2 se ve
`error while loading shared libraries: libassimp-6.dll: ...`, y el exit code
es `0xC0000135`).

## Uso rápido

```bash
FunshiCLI help
FunshiCLI version
FunshiCLI --tui
FunshiCLI --chat                # Chat Silvy directo (pestaña Chat en TUI)
FunshiCLI --request < solicitud.json
FunshiCLI --request-file solicitud.json
```

`--tui` abre una interfaz interactiva de terminal con **6 pestañas**:
Proyectos, Escena, Scripts, Diagnósticos, **Chat Silvy**, **Modelos HF**.
No admite solicitud adjunta. Necesita una terminal interactiva:
sin ella responde `tui_terminal_unavailable` y sale con código 2.

`--chat` abre la TUI directamente en la pestaña **Chat Silvy**.

Ejemplo — crear un proyecto 2D:

```bash
echo '{"command":"project.create","args":{"name":"MiJuego2D","dimension":2}}' \
  | FunshiCLI --request
```

Respuesta:

```json
{"apiVersion":1,"error":null,"ok":true,"result":{"dimension":2,"name":"MiJuego2D","path":".../MotorGrafico/Proyects/MiJuego2D"}}
```

## Agente Silvy (Chat)

En la pestaña **Chat Silvy** (o con `--chat`):

| Escribe | Qué hace |
|---|---|
| `/project.list` | Comando CLI directo (prefijo `/`) |
| `crear proyecto MiJuego 2d` | Lenguaje natural → LLM ejecuta tools |
| `listar escenas` | Lenguaje natural → LLM |
| `diagnosticos` | LLM o CLI según prefijo |

**Sin servidor LLM configurado**: solo funcionan comandos con `/`.
**Con servidor LLM**: el texto natural se envía al modelo, que razona y ejecuta tools del motor.

### Configurar LLM (desde la TUI)

1. Pestaña **Modelos HF** (Tab/→ 6 veces)
2. Buscar modelo (ej: `qwen2.5 gguf`, `phi-4-mini`)
3. Enter en modelo → ver variantes (archivo | GB | quantization)
4. Enter en variante → **auto**: descarga GGUF → verifica SHA256 → compila llama.cpp → levanta llama-server → configura Silvy
5. Ver indicador en Chat: `[LOCAL: modelo]` y barra superior: `🟢 LOCAL: modelo`

### Servidores LLM compatibles

| Tipo | Ejemplo | Config |
|---|---|---|
| **Local (auto)** | `bartowski/Qwen2.5-7B-Instruct-GGUF` | Todo automático desde Modelos HF |
| **Ollama** | `http://localhost:11434/v1` | `ollama pull qwen2.5:7b && ollama serve` |
| **LM Studio** | `http://localhost:1234/v1` | Cargar GGUF → "Start Server" |
| **vLLM / OpenAI** | `http://localhost:8000/v1` | API key si es remoto |

## Documentación

- [MANUAL_DE_USO.md](MANUAL_DE_USO.md) — referencia completa de comandos, argumentos, formatos de respuesta y cód de salida.
- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) — arquitectura por capas y relación con el motor.

## Tests

```bash
cd build && ctest -R cli-tests --output-on-failure
```

## Licencia

Apache License 2.0 — ver [LICENSE](LICENSE).
