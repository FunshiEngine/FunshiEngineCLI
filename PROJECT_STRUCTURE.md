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
    └── InterfazTui.h/.cpp    #   ejecutarTui(): pantalla completa con FTXUI sobre el núcleo
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

## Relación con el motor

- **`extern/FunshiEngine`** es un submódulo apuntando al motor
  (`FunshiEngine/FunshiEngineOpenGL`, rama `develop`).
- El CMake del CLI fija `BUILD_ENGINE=OFF`, `BUILD_FUNSHI_CLI=OFF` y
  `BUILD_RUNTIME=OFF` antes de `add_subdirectory` para no duplicar targets con
  los que el motor define dentro del submódulo.
- La librería estática `funshi_engine` se enlaza desde un build preexistente
  del motor (ver `find_library` en `CMakeLists.txt`). Los targets
  `funshi_project_core` y `funshi_diagnostics_core` se compilan desde el
  submódulo.
- La CLI consume la API interna del motor (proyectos, escenas, assets, scripts,
  diagnósticos). Si esa API cambia, hay que reconstruir el motor y volver a
  enlazar.

## Compatibilidad multiplataforma

El CLI funciona en **Linux y Windows**, igual que el motor. Las rutas se manejan
con `std::filesystem` (separador nativo), y el código que diverge por plataforma
usa guardas `#ifdef _WIN32`. El binario se nombra `FunshiCLI.exe` en Windows y
`FunshiCLI` en Linux; los tests resuelven el nombre según la plataforma.
