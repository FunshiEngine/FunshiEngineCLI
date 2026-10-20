# Estructura del Proyecto — Funshi CLI

Funshi CLI es el front-end de línea de comandos del motor FunshiEngineGL. El
motor (y toda su API interna) vive en un repositorio aparte
([FunshiEngineOpenGL](https://github.com/FunshiEngine/FunshiEngineOpenGL)) y se
consume aquí como **submódulo** en `extern/FunshiEngine`. Este repositorio
contiene solo el front-end CLI y su núcleo de lógica reutilizable.

## Arquitectura por capas

El código está separado en capas para que una futura app de escritorio pueda
reutilizar toda la lógica sin pasar por stdin/stdout. La regla que sostiene el
diseño: **el núcleo no toca stdout ni `argv`**; esa responsabilidad es del
front-end.

```
src/
├── main.cpp                  # Front-end: argv + lectura de la solicitud + escritura a stdout
├── Nucleo/                   # Lógica central reutilizable (agnóstica del transporte)
│   ├── Solicitud.h           #   Petición de operación (comando, args, id, proyecto)
│   ├── Respuesta.h/.cpp      #   Respuesta + código de salida (ResultadoComando)
│   ├── CatalogoComandos.h/.cpp  # enum class ComandoCLI + mapeo texto <-> enum
│   ├── Registro.h/.cpp       #   Política de registro de errores (uso vs operacional)
│   └── NucleoCLI.h/.cpp      #   ejecutarSolicitud(): dispatch por enum + switch exhaustivo
├── Validacion/               # Validación de argumentos por tabla de requisitos
│   └── ValidacionArgs.h/.cpp
├── Proyectos/                # Resolución y validación de rutas de proyecto
│   └── ResolucionProyectos.h/.cpp
├── Escena/                   # Contexto de escena (RAII) y serialización a JSON
│   ├── ContextoEscena.h/.cpp
│   └── SerializacionEscena.h/.cpp
├── Comandos/                 # Un módulo por dominio de comando
│   ├── ComandosProyecto.*    #   project.create / project.list / project.config.get
│   ├── ComandosEscena.*      #   scene.list / objects / object.get / hierarchy
│   ├── ComandosAssets.*      #   asset.import
│   ├── ComandosScripts.*     #   script.list / script.compile
│   ├── ComandosDiagnosticos.*#   diagnostics.list / diagnostics.clear
│   └── ComandosArchivos.*    #   project.src.create / dir.import / fs.create / file.create
└── Tui/                      # Interfaz interactiva de terminal (flag --tui)
    ├── InterfazTui.h/.cpp    #   ejecutarTui(): pantalla completa con FTXUI sobre el núcleo
    └── EntradaRespaldo.h/.cpp#   teclas en terminales sin consola de Windows (stty + parser FTXUI)
```

## Flujo de una solicitud

1. **`main.cpp`** parsea `argv` (`--request`, `--request-file`, `--project`,
   `--tui`), lee el JSON de la solicitud (stdin o archivo) y construye un
   `Solicitud`. Con `--tui` en vez de solicitar JSON, llama a
   `funshi::ejecutarTui()` (`Tui/InterfazTui.cpp`).
2. Llama a **`funshi::ejecutarSolicitud(solicitud)`** (`Nucleo/NucleoCLI.cpp`).
3. El dispatcher hace un `switch` exhaustivo sobre el `enum class ComandoCLI`
   y delega al handler del dominio correspondiente (`src/Comandos/...`).
4. Cada handler valida sus argumentos (`Validacion/`), resuelve el proyecto si
   hace falta (`Proyectos/`), ejecuta la operación y devuelve un
   `ResultadoComando` (respuesta + código de salida).
5. **`main.cpp`** escribe la respuesta a stdout y devuelve el código de salida.

La TUI da los mismos pasos 2 a 4 construyendo la `Solicitud` desde su
interfaz y presentando cada `ResultadoComando` en pantalla; el camino JSON de
stdin/stdout es un front-end más sobre el mismo núcleo.

## Decisiones de diseño

- **Núcleo agnóstico del transporte.** `ejecutarSolicitud()` no imprime nada.
   El resultado viaja en un `ResultadoComando` (una `Respuesta` más el código
   de salida). Esto es lo que permite reutilizar la lógica desde una UI.
- **Catálogo cerrado de comandos.** Cada comando es un valor de
   `enum class ComandoCLI` (`Nucleo/CatalogoComandos.h`). El `switch` de
   dispatch es exhaustivo, de modo que el compilador avisa al agregar un valor
   sin cubrir. El mapa texto <-> enum vive en un solo lugar.
- **Validación por tabla.** Cada comando declara sus argumentos como una tabla
   de `RequisitoArg` (nombre, obligatoriedad, tipo) y la validación es una sola
   función. Un comando nuevo agrega su fila en vez de repetir
   `if (!argumentos.contains(...))` a mano.
- **Contexto de escena RAII.** `ContextoEscena` construye el mismo armazón que
   el editor (registro, física, eventos, assets, controlador, serializador) y lo
   destruye en orden inverso al salir. Los comandos de escena lo usan en vez de
   armar el contexto a mano, así todos comparten una sola vía de carga.
- **Política de registro centralizada.** `Nucleo/Registro.h` distingue entre
   error de uso (solo se comunica al cliente) y error operacional (también se
   registra en el historial compartido que consulta la GUI del motor).
- **La TUI consume el núcleo, no stdout.** `funshi::ejecutarTui()`
   (`Tui/InterfazTui.cpp`) construye sus `Solicitud` y presenta sus
   `ResultadoComando` sin pasar por stdin/stdout. FTXUI (v7.0.3, MIT) llega
   como dependencia con `FetchContent` y tag fijo en `CMakeLists.txt`.
- **La TUI se organiza en cinco piezas.** Barra superior, panel lateral de
   dominios, panel central con los datos de la sección, caja de comandos y
   barra de estado. La caja de comandos es un `ftxui::Input` que la propia
   interfaz enfoca y desenfoca (`ActiveChild()` decide cuál tiene el foco), de
   modo que el panel y la entrada nunca compiten por las teclas.
- **El comando se ejecuta de forma síncrona.** `Enter` se atiende dentro de
   `OnEvent` en vez de apoyarse en `on_enter` del input: FTXUI difiere ese
   callback a la cola de tareas, con lo que el comando se ejecutaría con el
   texto ya modificado si más teclas llegaran en el mismo lote.
- **El texto se mide en columnas, no en bytes.** `recortar()` y la línea de
   atajos usan `ftxui::string_width`, que cuenta celdas de pantalla, para que
   un texto con acentos o flechas no se corte a media palabra. La línea de
   atajos tiene tres variantes y se usa la primera que cabe en el ancho de la
   terminal.
- **La TUI lee teclas aun sin consola de Windows.** `EntradaRespaldo`
   (`Tui/EntradaRespaldo.cpp`) clasifica la entrada con
   `clasificarEntradaTui()`: en consola nativa las teclas las lee FTXUI con la
   API de consola; en un pty de MSYS2 o Git Bash, donde esa API no está
   disponible, el módulo aplica modo crudo con `stty` y traduce los bytes con
   el parser interno de FTXUI, de modo que ambas rutas producen los mismos
   eventos. Sin terminal interactiva, la TUI no arranca y `main.cpp` responde
   `tui_terminal_unavailable` (código de salida 2).

## Relación con el motor

- **`extern/FunshiEngine`** es un submódulo apuntando al motor
  (`FunshiEngine/FunshiEngineOpenGL`, rama `develop`).
- El CMake del CLI fija `BUILD_ENGINE=ON`, `BUILD_FUNSHI_CLI=OFF`,
  `BUILD_RUNTIME=ON` y `ENABLE_ASAN=OFF` antes de `add_subdirectory`: el motor
  se compila desde el submódulo y aporta los targets `funshi_engine`,
  `funshi_project_core` y `funshi_diagnostics_core`. Con `BUILD_RUNTIME=ON`
  el motor construye solo su biblioteca: el ejecutable del editor y las
  pruebas headless del motor pertenecen a su propio repositorio.
- La CLI enlaza contra ese mismo `funshi_engine` del submódulo, de modo que
  las cabeceras con las que compila y la biblioteca que enlaza salen siempre
  del mismo commit. Enlazarlas desde builds distintos rompe el proceso: los
  structs que cruzan la frontera (por ejemplo `ConfigPersistence::Proyecto`,
  que se devuelve por valor) tienen layouts distintos y corrompen la memoria
  al liberarse.
- La CLI consume la API interna del motor (proyectos, escenas, assets, scripts,
  diagnósticos). Si esa API cambia, hay que actualizar el submódulo y
  reconstruir la CLI.

## Compatibilidad multiplataforma

El CLI funciona en **Linux y Windows**, igual que el motor. Las rutas se manejan
con `std::filesystem` (separador nativo), y el código que diverge por plataforma
usa guardas `#ifdef _WIN32`. El binario se nombra `FunshiCLI.exe` en Windows y
`FunshiCLI` en Linux; los tests resuelven el nombre según la plataforma.
