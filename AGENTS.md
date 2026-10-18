# AGENTS.md — Convenciones para agentes de código

Guía rápida para agentes (y humanos) que trabajen en este repositorio.

FunshiEngineCLI es la interfaz de línea de comandos del motor FunshiEngineGL.
El motor (y toda su API interna) vive en un repositorio aparte
(`FunshiEngine/FunshiEngineOpenGL`) y se consume aquí como **submódulo** en
`extern/FunshiEngine`. Este repo contiene solo el front-end CLI y su núcleo de
lógica reutilizable.

## Comandos esenciales

```bash
# Inicializar el submódulo del motor (una vez por clon)
git submodule update --init --recursive

# Configurar y compilar (CMake es el único build soportado)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Ejecutar la suite de tests
cd build && ctest --output-on-failure
```

- **Submódulo `extern/FunshiEngine`**: el CLI compila contra la API del motor
  (headers y librerías estáticas). El CMake del CLI fija
  `BUILD_ENGINE=OFF`, `BUILD_FUNSHI_CLI=OFF` y `BUILD_RUNTIME=OFF` antes de
  `add_subdirectory` para evitar colisión de targets con los que el motor
  define dentro del submódulo (`FunshiCLI`/`cli-tests` siguen existiendo ahí,
  pero no se generan en este build).
- **Librería `funshi_engine`**: se enlaza desde un build preexistente del motor
  (ver `find_library` en `CMakeLists.txt`). Si cambia la API del motor, hay que
  reconstruir el motor en su repo y volver a enlazar.
- **`cmake` puede no estar en el PATH**: en Windows/MSYS2 usar
  `C:/msys64/mingw64/bin/cmake.exe` con `PATH=/mingw64/bin:$PATH`, y el resto
  del toolchain de `mingw64` (`ninja`, `g++`).
- **Build y tests como parte del cambio**: al tocar código fuente o
  `CMakeLists.txt`, revisar SIEMPRE que los `.cpp` nuevos estén listados en el
  target (`FunshiCLI` lista sus fuentes explícitamente; no hay glob). Un `.cpp`
  olvidado compila a medias o falla en el link. Ajustar el build en el mismo
  commit que el código que lo motiva.

## Pruebas

- **Autoría de un test headless**: seguir el patrón de `tests/CliTests.cpp` —
  macro `CHECK(cond, msg)` definida en el propio archivo,
  `TempPruebas::CarpetaPrueba` para una carpeta temporal que se limpia sola al
  salir (RAII, disponible en el submódulo del motor), y cierre con
  `std::cout << (fallos == 0 ? "OK" : "FALLOS") << ": " << ... << " comprobaciones"`,
  saliendo con 0 o 1.
- **Código de salida 77 = *skipped*, no fallo**. El test binario espera encontrar
  el ejecutable `FunshiCLI` junto a sí mismo; si no está, se salta en lugar de
  fallar. `ctest` lo reporta aparte; no es un test roto.
- **La CLI es headless**: no hay UI que probar a mano todavía. El criterio de
  aceptación de un cambio de comportamiento se valida invocando la CLI con una
  solicitud JSON y comparando la respuesta y el exit code.

## Arquitectura del CLI

El front-end está modularizado para que una futura UI de escritorio pueda
reutilizar la lógica sin pasar por stdout. La separación por capas:

```
src/
├── main.cpp                # Solo argv + lectura de solicitud + escritura a stdout
├── Nucleo/                 # Lógica central reutilizable (agnóstica de transporte)
│   ├── Solicitud.h         #   Petición de operación (comando, args, id, proyecto)
│   ├── Respuesta.h/.cpp    #   Respuesta + código de salida (ResultadoComando)
│   ├── CatalogoComandos.h/.cpp  # enum class ComandoCLI + mapeo texto<->enum
│   ├── Registro.h/.cpp     #   Política de registro de errores (uso vs operacional)
│   └── NucleoCLI.h/.cpp    #   ejecutarSolicitud(): dispatch por enum + switch
├── Validacion/             # Validación de argumentos por tabla de requisitos
├── Proyectos/              # Resolución/validación de rutas de proyecto
├── Escena/                 # Contexto de escena (RAII) + serialización a JSON
└── Comandos/               # Un módulo por dominio de comando
    ├── ComandosProyecto.*  #   project.create/list/config.get
    ├── ComandosEscena.*    #   scene.list/objects/object.get/hierarchy
    ├── ComandosAssets.*    #   asset.import
    ├── ComandosScripts.*   #   script.list/compile
    ├── ComandosDiagnosticos.*  # diagnostics.list/clear
    └── ComandosArchivos.*  #   project.src.create/dir.import/fs.create, file.create
```

Reglas que sostienen esta arquitectura:

- **`main.cpp` no contiene lógica de comandos.** Solo parsea argv, lee el JSON
  de la solicitud (stdin o `--request-file`), llama a
  `funshi::ejecutarSolicitud()` y escribe la respuesta a stdout con su exit
  code. Toda la lógica vive en `src/Nucleo` y `src/Comandos`.
- **El núcleo no toca stdout.** `ejecutarSolicitud()` devuelve un
  `ResultadoComando` (respuesta + código de salida) sin imprimir nada. Así una
  UI de escritorio puede llamar al mismo núcleo y decidir cómo presentar el
  resultado. La política de "qué es error de uso vs error operacional" vive en
  `Nucleo/Registro.*`.
- **Comandos registrados en el catálogo.** Cada comando nuevo se agrega al
  `enum class ComandoCLI`, al mapa de texto en `CatalogoComandos.cpp`, a
  `todosLosComandos()` y a su caso en el `switch` de `NucleoCLI.cpp`. El
  switch es exhaustivo: el compilador avisa si falta un caso.
- **Un módulo de comandos por dominio.** Cada handler recibe los `args` ya
  extraídos (`const nlohmann::json&`) y devuelve un `ResultadoComando`. La
  validación de argumentos se hace con la tabla de `Validacion/ValidacionArgs.*`;
  no se repiten `if (!argumentos.contains(...))` a mano en cada comando.
- **`apiVersion` en las respuestas.** El campo `apiVersion` (en `Respuesta.h`)
  se incrementa al agregar funcionalidad; los campos de respuesta nuevos van al
  final. Las tablas de API del motor (`ApiScriptGameObject`, `ScriptServices`)
  son estrictamente APPEND-ONLY.

## Convenciones del código

- **Idioma**: C++17. Identificadores, comentarios y logs en español
  (`ejecutarSolicitud`, `resolverProyectoPorNombre`). Nombres de tipo en
  PascalCase, métodos en camelCase, espacios de nombres en minúsculas
  (`funshi`).
- **Copyright**: todo archivo fuente nuevo debe llevar el header Apache 2.0
  (ver cabecera de cualquier `.h`/`.cpp` existente como modelo) o al menos
  `SPDX-License-Identifier: Apache-2.0`.
- **Estilo**: 4 espacios, llaves en la línea siguiente para clases/funciones.
- **Sin referencias de gestión en los textos del proyecto.** Los comentarios del
  código y la documentación técnica describen **piezas, comportamiento,
  contratos y causas técnicas, nada más**. No llevan identificadores de tareas o
  de bugs, planes, fases, hitos, fechas, estados ("pendiente", "por hacer") ni
  alusiones a documentos ajenos al texto. Ese contexto pertenece a los
  documentos de gestión — plan, issues, historial de cambios y mensajes de
  commit/PR —, donde sí debe quedar.
- **Enums y switch-case**: usar `enum class` y `switch` exhaustivo (con
  `default` que lance o loguee) en lugar de cadenas de `if-else` sobre strings o
  magic numbers. El compilador avisa de casos no cubiertos al agregar valores y
  evita errores de typo. Aplicar al agregar nuevos comandos, tipos de respuesta,
  o cualquier conjunto cerrado de valores.

## Reglas de arquitectura

- **Análisis previo obligatorio**: antes de crear o modificar código, analizar
  las excepciones, estructuras de datos, patrones de diseño y arquitecturas ya
  presentes en el proyecto (y en el submódulo del motor) y reutilizarlas; si el
  cambio requiere algo nuevo o distinto, recomendarlo y justificarlo antes de
  implementarlo.
- **El motor es una dependencia, no se edita aquí.** Toda la API interna
  (`funshi_engine`, `funshi_project_core`, `funshi_diagnostics_core`, clases de
  `src/` del motor) vive en `FunshiEngineOpenGL`. Este repo solo consume headers
  y librerías. Si hace falta cambiar el motor, se hace en su repo (rama
  `develop`) y se actualiza el submódulo.
- **Compatibilidad multiplataforma**: el CLI debe funcionar en **Linux y
  Windows**, igual que el motor. Al usar `std::filesystem`, APIs del SO o
  rutas, verificar ambas plataformas. Usar el separador nativo vía
  `std::filesystem::path`, y `#ifdef _WIN32` / `#else` cuando el contrato
  difiera. Las rutas que el motor resuelve contra el cwd o la ubicación fija de
  proyectos son la zona de mayor divergencia.
- **Modularidad para la UI futura**: el núcleo (`src/Nucleo`) no debe acoplarse
  a stdout, a `argv` ni a la consola. Cualquier servicio nuevo que la CLI
  exponga debe llegar como función del núcleo que devuelva `ResultadoComando`,
  de modo que una UI de escritorio pueda llamarla directamente.

## Memoria de trabajo: documentos locales ignorados por git

Todo el estado local que un agente necesita para retomar el trabajo sin
re-investigarlo desde cero vive bajo **una única carpeta ignorada por git**,
`.agentes/`, en la raíz del repo. No es documentación del proyecto ni se
versiona; es la memoria de trabajo del agente.

| Ruta | Para qué sirve |
|---|---|
| `.agentes/MEMORIA.md` | Memoria de contexto entre sesiones: estado del trabajo en curso, sistemas del código ya analizados, supuestos abiertos, hallazgos pendientes, comandos de build/test que funcionan en el entorno y archivos que no se deben tocar. |

Reglas:

- **Crear lo que haga falta, manteniendo la raíz ordenada**: `.agentes/` es
  extensible. No crear archivos sueltos en la raíz del repo: la raíz es del
  proyecto, `.agentes/` es del agente.
- **Si falta un archivo, recrearlo**: reconstruir su contenido desde el estado
  real (git log, código fuente, esta guía) y tratarlo desde ese momento como la
  fuente de contexto de la tarea.
- **Leerlos al arrancar** una tarea que toque temas que cubren, y **escribir en
  ellos** lo nuevo relevante de forma incremental durante el trabajo.
- **Actualizarlos al cerrar cada paso**: lo que quedó hecho, verificado y con
  qué commit queda registrado ahí.
- Al commitear, **nunca** incluir `.agentes/` (está ignorada a propósito) ni
  `extern/` (es el submódulo); un `git add <ruta>` explícito de los archivos
  propios de la tarea es la única forma correcta de agregar cambios.

## Flujo de trabajo

- **Lista de tareas antes de escribir código**: si la tarea requiere más de un
  paso (analizar, tocar varios archivos, testear, documentar), armar primero una
  lista de pendientes explícita y trabajar contra ella, no de memoria. La lista
  se mantiene al día de forma incremental: cada tarea se marca como completada
  en cuanto está hecha y verificada, y no todas al final.
- **Una tarea, una rama**: al comenzar una tarea de un tema distinto al actual,
  primero commitear los cambios pendientes de la rama actual y luego cambiar de
  rama. Nunca mezclar temas distintos en una misma rama.
- **Rama desactualizada: ponerla al día antes de escribir código**: nada más
  crear o traer la rama —y también cada vez que se retome el trabajo en ella—
  hacer `git fetch` y actualizarla con `git merge --ff-only origin/<rama>` o
  `git rebase origin/<rama>`. Nunca dejar que la tarea se siga sobre un árbol
  viejo ni mezclar el atraso al final. Si la actualización produce conflictos
  por commits ajenos, reportarlos y esperar instrucciones.
- **Documentación sincronizada**: la documentación es parte de la tarea. Antes
  de empezar, revisar los `.md` que describen el área afectada (`README.md`,
  `MANUAL_DE_USO.md`, `PROJECT_STRUCTURE.md` cuando existan) y, si el cambio
  agrega un `.cpp`, una dependencia o un target, mantener el `CMakeLists.txt` al
  día en el mismo commit.
- **Commits atómicos por tarea**: cada tarea terminada cierra con su commit (o
  los que sean necesarios si la tarea es grande), con mensaje descriptivo y
  convencionales (`feat:`, `fix:`, `docs:`, `refactor:`, `chore:`).
- **Commits de funcionalidades grandes incompletas**: si se guarda una
  funcionalidad todavía no terminada o validada, el cuerpo del mensaje debe
  detallar explícitamente las insuficiencias: qué falta, qué validaciones se
  hicieron y cuáles siguen pendientes, y qué limita su cierre.
- **Sin atribuciones ajenas al cambio**: los mensajes de commit (y cualquier
  metadato asociado) describen únicamente la implementación o los cambios
  realizados. No se adjudica coautoría, autoría, firmas ni menciones a
  herramientas o terceros que no correspondan al trabajo concreto sobre el
  código.
- **Sin coautorías**: nunca agregar trailers `Co-authored-by` a los mensajes de
  commit.
- **Commits solo con archivos propios**: al commitear, agregar únicamente los
  archivos que modificó el agente en la tarea actual (`git add <archivos>`), NO
  usar `git add -A` ni `git add .` que incluyen cambios ajenos.
- **Comandos destructivos requieren permiso y justificación**: antes de ejecutar
  `git checkout`, `git reset`, `git restore`, `git clean` o cualquier comando
  que descarte cambios, pedir permiso explícito al usuario, argumentar en
  español por qué es necesario y qué se perderá, y esperar confirmación.
- **Validar antes de commitear**: build completo + `ctest` en verde. Si el
  cambio toca la lógica de un comando, además invocar la CLI con una solicitud
  real y verificar respuesta y exit code.
- **NO confirmar fixes hasta validación del usuario**: nunca declarar un cambio
  como "resuelto" o "listo" hasta que el usuario lo pruebe y lo confirme
  explícitamente. Los tests automatizados no cubren todos los flujos; el
  criterio de aceptación lo define el usuario usando la aplicación real.
