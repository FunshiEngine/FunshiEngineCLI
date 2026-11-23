# Arquitectura — FunshiEngineCLI

Documento de diseño de la CLI: qué piezas la forman, cómo dependen unas de
otras, cómo fluye una petición y qué reglas mantienen limpia la estructura.

- Árbol de archivos y relación con el motor: [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)
- Diagramas UML (componentes, secuencias, estados, despliegue): [uml/ArquitecturaUML.es.md](uml/ArquitecturaUML.es.md)
- Diagramas de clases: [uml/DiagramaClases.es.md](uml/DiagramaClases.es.md)
- Uso de comandos y de la TUI: [MANUAL_DE_USO.md](MANUAL_DE_USO.md)

## 1. Propósito y contexto

FunshiEngineCLI automatiza el motor **FunshiEngineGL** (proyectos, escenas,
scripts, assets, diagnósticos) y aloja a **Silvy**, un agente que traduce
lenguaje natural en esos mismos comandos con un modelo LLM local (llama.cpp)
o remoto (API compatible con OpenAI).

Tiene tres front-ends sobre un único núcleo:

| Front-end | Entrada | Uso típico |
|---|---|---|
| JSON (`--request`, `--request-file`) | una `Solicitud` en JSON | scripts, CI, el propio motor |
| TUI (`--tui`, `--chat`) | teclado y ratón (FTXUI) | uso interactivo, chat con Silvy |
| Agente (`agente.procesar`, chat) | texto libre | Silvy decide qué comandos ejecutar |

Sistemas externos: el motor (submódulo `extern/FunshiEngine`, enlazado
estáticamente), Hugging Face (búsqueda y descarga de modelos GGUF), GitHub
(binarios oficiales de `llama-server`) y el proceso `llama-server` que la CLI
lanza y controla.

## 2. Capas y regla de dependencias

```
 Front-ends      main.cpp (JSON)        Tui/ (FTXUI)
                      │                     │
 Agente               │        Agente/ (Silvy, Harness, Auditoria)
                      │             │            │
                      │             │      Agente/LLM/ (Bucle, Cliente, Config,
                      │             │       Herramientas, HuggingFace, Local)
                      ▼             ▼
 Núcleo          Nucleo/ (Solicitud → ejecutarSolicitud → ResultadoComando)
                      │
 Dominio         Comandos/ ── Validacion/ · Proyectos/ · Escena/
                      │
 Motor           extern/FunshiEngine (funshi_engine, project_core, diagnostics_core)
```

Reglas:

1. **Las dependencias solo apuntan hacia abajo.** `Nucleo/` no conoce la TUI
   ni al agente; `Comandos/` no conoce a sus llamadores.
2. **El núcleo no toca `stdout` ni `argv`.** Devuelve un `ResultadoComando`
   (respuesta + código de salida) y el front-end decide cómo mostrarlo.
3. **El agente usa el mismo camino que un usuario.** Las herramientas del LLM
   (`EngineTool`) arman una `Solicitud` y la pasan por `Harness`, que llama a
   `ejecutarSolicitud()` y audita. No hay atajos hacia el motor.
4. **`Agente/LLM/` no depende de la TUI.** La TUI recibe el progreso por
   callbacks (`ProgressCallback`, `AgentEvent`) y redibuja con `PostEvent`.

## 3. Módulos

| Módulo | Responsabilidad | API principal | Depende de |
|---|---|---|---|
| `Nucleo/` | Modelo de petición/respuesta, catálogo cerrado de comandos y dispatch | `Solicitud`, `ResultadoComando`, `ComandoCLI`, `ejecutarSolicitud()` | `Comandos/` |
| `Validacion/` | Validación de argumentos por tabla | `validarArgumentos()`, `argumentoProyecto()` | — |
| `Proyectos/` | Resolver un proyecto por nombre o ruta | `resolverProyectoPorNombre()` | motor |
| `Escena/` | Armazón RAII de escena y serialización a JSON | `ContextoEscena`, `sceneHierarchyJson()` | motor |
| `Comandos/` | Un manejador por comando (`ResultadoComando f(const json&)`) | `escena*`, `proyecto*`, `script*`… | Validacion, Proyectos, Escena, motor |
| `Agente/` | Silvy (enrutado), Harness (ejecución auditada), Auditoría | `Silvy::procesar()`, `Harness::ejecutar()` | Nucleo, Agente/LLM |
| `Agente/LLM/Config` | Configuración persistida y tipos del chat | `LlmConfig`, `ChatMessage`, `AgentEvent` | — |
| `Agente/LLM/Cliente` | Cliente HTTP OpenAI-compatible con streaming SSE; descargas | `LlmGateway`, `http::descargar()` | libcurl |
| `Agente/LLM/Bucle` | Bucle modelo → herramientas → modelo | `AgentLoop`, `ToolRegistry`, `PromptToolParser` | Cliente, Config |
| `Agente/LLM/Herramientas` | Comandos del motor expuestos como tools | `EngineTool`, `createEngineToolRegistry()` | Bucle, Harness |
| `Agente/LLM/HuggingFace` | Búsqueda, metadatos, variantes y descarga verificada | `HfApi` | Cliente |
| `Agente/LLM/Local` | Binario llama-server, modelos locales, ciclo de vida del servidor | `LocalInferenceManager`, `InstaladorLlama`, `RegistroServidor` | Cliente, HuggingFace |
| `Tui/` | Interfaz FTXUI: secciones, chat, modelos, ayuda | `ejecutarTui()`, `AplicacionTui` | Nucleo, Agente |
| `Util/` | Portapapeles (herramienta del sistema u OSC 52), métricas | `util::Clipboard::copiar()`, `secuenciaOsc52()`, `metricas::Cronometro` | — |

## 4. Flujos principales

Los diagramas de secuencia de cada flujo están en
[uml/ArquitecturaUML.es.md](uml/ArquitecturaUML.es.md#3-diagramas-de-secuencia).

**Petición JSON.** `main.cpp` lee la `Solicitud` → `ejecutarSolicitud()`
traduce el texto a `ComandoCLI` → `switch` exhaustivo → manejador de
`Comandos/` → valida, resuelve el proyecto, opera sobre el motor →
`ResultadoComando` → `main.cpp` imprime JSON y sale con su código.

**Chat con Silvy.** La TUI lanza un hilo por orden → `Silvy::procesar()`:
- texto con `/` al inicio: comando CLI directo vía `Harness`;
- resto: `AgentLoop::run()` envía el historial al `LlmGateway` en streaming;
  cada tool call se sanea (`ToolCallSanitizer`), se ejecuta como `EngineTool`
  (→ `Harness` → núcleo) y su resultado vuelve al modelo, hasta 8 rondas.
  Los eventos (`Text`, `ToolStart`, `ToolEnd`) actualizan el chat en vivo.

Si la plantilla del modelo no admite tools nativas (`/props` de
llama-server), el bucle usa el **modo prompt**: las herramientas se describen
en el prompt de sistema y `PromptToolParser` extrae bloques ```` ```tool_call ````
del texto.

**Instalar y activar un modelo.** Sección Modelos → «Descargados» (archivo ya
en disco) o «Buscar en Hugging Face» (búsqueda → variantes → descarga
reanudable a `.part` con SHA256) → `InstaladorLlama::asegurarActualizado()`
(binario oficial, con caché de la consulta de 10 min y modo sin conexión) →
`LocalInferenceManager::startServer()` (detiene el servidor anterior, lanza
`llama-server`, espera `/health`, registra el PID) → consulta de capacidades
→ `Silvy::configurarLlm()` → `LlmConfig::save()`.

**Ciclo de vida de `llama-server`.** El proceso se lanza desacoplado para que
sobreviva a la petición que lo creó. `RegistroServidor` guarda
`llama-server.pid` (PID del servidor + PID dueño) y garantiza que:
- al salir de la TUI (Esc, `q`, Ctrl+C) se detiene;
- ante `SIGTERM`/`SIGHUP`/`SIGINT`/`SIGQUIT` el manejador lo mata antes de terminar;
- al cambiar de modelo se detiene el anterior;
- al iniciar se limpian los servidores huérfanos de sesiones ya cerradas.

## 5. Concurrencia

- **Hilo de UI**: el bucle de FTXUI. Solo él dibuja y procesa eventos.
- **Hilos de trabajo desacoplados** (`std::thread(...).detach()`): orden de
  chat, búsqueda HF, carga de variantes, listado de modelos locales,
  instalación de modelo, actualización de `llama-server` al arrancar y copia
  al portapapeles (lanza `xclip`/`wl-copy`).
- **Selección con el ratón**: el ratón solo guarda coordenadas; un nodo que
  envuelve la pantalla pinta el resaltado y lee el texto de las celdas ya
  dibujadas, y la copia se dispara con un evento `Custom` fuera del dibujado.
  La secuencia OSC 52 se escribe desde el hilo de UI, entre cuadros, para no
  mezclarse con la salida de FTXUI.
- **Comunicación**: los hilos escriben estado bajo un mutex
  (`modelosHFMutex_`, `chatMutex_`) y piden redibujo con
  `pantalla_->PostEvent(Event())`. El chat limita los redibujos a ~30 por segundo.
- **Estado compartido con hilos desacoplados**: `EstadoCompartido` (con la
  pantalla protegida por mutex) evita que un hilo toque la pantalla después de
  destruida; los estáticos que usan esos hilos se crean con `new` y no se
  destruyen nunca, para que sigan válidos durante la salida del proceso.
- **cURL** se inicializa una vez en `main` (`http::inicializarCurl()`) y todas
  las transferencias usan `CURLOPT_NOSIGNAL`.
- **Cancelación** de una respuesta en curso: `LlmGateway::setCancelFlag()`
  (el callback de progreso de cURL aborta la transferencia).

## 6. Datos en disco

| Archivo | Ubicación por defecto | Escribe |
|---|---|---|
| `llm-config.json` | `$FUNSHI_CONFIG_DIR`, o `.agentes/` del cwd o de sus padres, o global (`~/.agentes`, `%APPDATA%\FunshiEngine`) | `LlmConfig::save()` (atómico) |
| `metricas.jsonl` | junto a `llm-config.json` | `metricas::registrar()` (con `FUNSHI_PERF=1`) |
| `silvy-auditoria.log` | `.agentes/` | `Auditoria` |
| Modelos `.gguf` (+ `.part`) | `modelsDir` o `~/.agentes/local-inference/models` | `LocalInferenceManager::downloadModel()` |
| `llama-bin/<tag>/`, `installed.json` | `~/.agentes/local-inference/` | `InstaladorLlama` |
| `llama-server.log`, `llama-server.pid` | `~/.agentes/local-inference/` | servidor / `RegistroServidor` |
| Caché de Hugging Face | `~/.agentes/hf-cache/` (TTL 24 h; vencida se usa sin conexión) | `HfApi` |
| `undo_history.json` | `<proyecto>/Memory/` | comandos de escena |

## 7. Errores y resiliencia

- **Errores de uso vs operacionales** (`Nucleo/Registro.h`): los de uso solo se
  devuelven al cliente; los operacionales además se registran en el historial
  de diagnósticos que consulta el editor. Códigos de salida en `codigosSalida`.
- **Sin conexión, nunca un crash**: HF usa la caché vencida; el instalador
  se queda con el binario ya instalado; la descarga se reanuda desde `.part`.
- **Archivos dañados**: `inspeccionarArchivoModelo()` detecta GGUF truncados
  leyendo solo la cabecera; no se pueden seleccionar.
- **Los hilos de la TUI capturan todo** (`catch (...)`) y muestran el error en
  la sección en lugar de terminar el proceso.

## 8. Puntos de extensión

**Agregar un comando CLI**
1. Valor nuevo en `ComandoCLI` y su texto en `CatalogoComandos.cpp`.
2. Manejador en el `Comandos/<Dominio>` que corresponda (`ResultadoComando f(const nlohmann::json&)`).
3. Caso en el `switch` de `NucleoCLI.cpp` (el compilador avisa si falta).
4. Ayuda en `Tui/Vistas/AyudaComandos.cpp` y fila en `doc/MANUAL_DE_USO.md`.
5. Si el agente debe poder usarlo: una clase `EngineTool` en `Agente/LLM/Herramientas/` registrada en `createEngineToolRegistry()`.

**Agregar una sección a la TUI**
1. Valor en `SeccionTui` (`Tui/Terminal/TemaTui.h`) y `kCantidadSecciones`.
2. Carga en `recargarSeccionActual()` (`AplicacionTui_Nav.cpp`).
3. Si tiene interacción propia, una subcarpeta `Tui/<Seccion>/` con sus eventos y su panel.

**Agregar un backend LLM**: implementar `LlmGateway` (`chatStream`, `chat`,
`listModels`, `close`) y crearlo en `Silvy::configurarLlm()`.

## 9. Convenciones de código

- **Tamaño**: ningún archivo de `src/` ni `tests/` supera ~350 líneas. Un módulo
  que crece se parte por responsabilidad (`Modulo_Parte.cpp`).
- **Internos**: lo compartido entre las partes de un módulo va en
  `ModuloInterno.h` (o `*Impl.h`) y no se incluye desde fuera del módulo.
- **Carpetas**: un dominio con más de dos archivos tiene su propia subcarpeta;
  los includes propios siempre van con ruta completa desde `src/`
  (`#include "Agente/LLM/Local/LocalInference.h"`).
- **Idioma**: identificadores y comentarios nuevos en español; el código
  heredado en inglés (`Agente/LLM`) se mantiene tal cual.
- **Plataformas**: Linux y Windows; lo específico va entre `#ifdef _WIN32`.
