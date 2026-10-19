# Manual de Uso — Funshi CLI

Funshi CLI ejecuta operaciones del motor FunshiEngineGL mediante solicitudes
JSON, sin abrir el editor gráfico. Sirve para automatizar la creación de
proyectos, la gestión de escenas y objetos, la importación de assets, la
compilación de scripts y la consulta del registro de diagnósticos.

La CLI es un front-end delgado sobre un núcleo de lógica reutilizable
(`src/Nucleo`), de modo que hoy se invoca por línea de comandos y en el
futuro una app de escritorio podrá llamar al mismo núcleo sin pasar por
stdin/stdout. Ver [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) para el detalle
de la arquitectura.

## Compilar

La CLI se clona con su submódulo del motor y se compila con CMake:

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

El ejecutable queda en `build/FunshiCLI` (Linux) o `build/FunshiCLI.exe`
(Windows).

> Nota: la CLI enlaza contra la librería estática `funshi_engine` de un build
> del motor. Si cambia la API del motor, reconstruí el motor en su repositorio
> y volvé a enlazar (ver `AGENTS.md`).

> Nota (Windows): el ejecutable carga al arrancar DLLs de runtime que no se
> copian a `build/`: las del toolchain MinGW-w64 (`libstdc++-6`,
> `libgcc_s_seh-1`, `libwinpthread-1`) y las del motor (`libassimp-6`,
> `glfw3`, las de Bullet), que en una instalación MSYS2 viven en
> `C:\msys64\mingw64\bin`. Ese directorio debe estar en el PATH: la terminal
> MINGW64 de MSYS2 ya lo trae; en UCRT64 o PowerShell hay que agregarlo
> antes de ejecutar (`export PATH=/mingw64/bin:$PATH` o
> `$env:Path += ";C:\msys64\mingw64\bin"`). Sin esas DLLs el proceso termina
> en el arranque sin imprimir nada —en shells MSYS2 se ve `error while
> loading shared libraries: libassimp-6.dll: ...`— y el prompt vuelve de
> inmediato.

## Ayuda y versión

```bash
FunshiCLI help     # texto de ayuda en prosa
FunshiCLI version  # solo la versión, una por línea
```

Ambos también existen como solicitud JSON (`{"command":"help"}` y
`{"command":"version"}`), que devuelven la respuesta estructurada.

## Interfaz TUI

`FunshiCLI --tui` abre una interfaz interactiva a pantalla completa que se
ajusta al tamaño de la terminal. No admite `--request` ni `--request-file`:
construye sus propias solicitudes contra el mismo núcleo que la CLI JSON.

La pantalla tiene cuatro piezas:

- **Barra superior**: versión de la CLI, directorio de trabajo y proyecto
  activo.
- **Panel lateral**: los dominios — Proyectos, Escena, Scripts y
  Diagnósticos.
- **Panel central**: los datos reales de la sección, pedidos al núcleo
  (`project.list`, `scene.list`, `script.list`, `diagnostics.list`).
- **Barra inferior**: el resultado de la última operación y los atajos.

Teclas:

| Tecla              | Acción                                              |
|--------------------|-----------------------------------------------------|
| `↑` / `↓`          | navegar la lista de la sección actual               |
| `←` / `→` / `Tab`  | cambiar de sección (recarga sus datos)              |
| `Enter`            | en Proyectos: fija el proyecto activo               |
| `r`                | recargar la sección actual                          |
| `q`, `Esc`, `Ctrl+C` | salir                                            |

Sin proyecto activo, las secciones Escena y Scripts piden elegir uno en
Proyectos (`Enter` sobre la fila).

### Requisitos de la terminal

En Windows, la TUI funciona en cualquier consola nativa (cmd, PowerShell,
Windows Terminal o mintty con ConPTY), donde las teclas se leen con la API de
consola. En un pty de MSYS2 o Git Bash sin ConPTY esa API no está disponible,
así que la CLI lee las teclas directamente de la entrada y aplica el modo
crudo con `stty` (que debe estar en el PATH, como en cualquier instalación de
MSYS2). En POSIX la entrada la resuelve el propio FTXUI. Además, en Windows
el ejecutable necesita las DLLs de MinGW-w64 y del motor en el PATH (ver
«Compilar»): si faltan, el proceso termina en el arranque y la TUI ni siquiera
llega a abrirse.

Sin una terminal interactiva —stdin redirigido desde un archivo o una
tubería, o lanzado desde un programa sin terminal— la TUI no tendría forma de
recibir teclas y se niega a arrancar:

```console
$ FunshiCLI --tui < archivo.json
{"apiVersion":1,"error":{"code":"tui_terminal_unavailable", ...}, ...}
$ echo $?
2
```

Limitaciones de la vía sin consola de Windows: el tamaño de la terminal se
detecta una sola vez al arrancar (un cambio posterior no se refleja), el
ratón queda fuera y las teclas pulsadas durante el primeros segundos de
arranque pueden perderse, porque las órdenes `stty` de la puesta en marcha
corren como procesos que heredan la entrada. Conviene esperar a que la
pantalla se pinte antes de navegar.

## Solicitudes JSON

Cada solicitud contiene un `command` y, si el comando necesita parámetros, un
objeto `args`. La solicitud se envía por stdin o desde un archivo:

```json
{
  "command": "project.create",
  "args": {
    "name": "MiProyecto",
    "dimension": 2
  }
}
```

```bash
FunshiCLI --request < crear-proyecto.json          # por stdin
FunshiCLI --request-file crear-proyecto.json       # por archivo
```

- `--request` lee la solicitud por stdin; `--request-file ARCHIVO` la lee de un
  archivo. Hay que indicar **exactamente uno** de los dos.
- `--project RUTA` hoy no tiene efecto: el proyecto se elige con `args.project`
  (nombre). Ver «Selección del proyecto».

### Selección del proyecto

Los comandos que trabajan sobre un proyecto lo identifican **por nombre**, en
`args.project` (por ejemplo `"project": "MiProyecto"`). El motor resuelve ese
nombre contra la raíz de proyectos que calcula en cada arranque, y esa raíz no
es fija: vive junto al ejecutable y, si esa carpeta no es escribible (por
ejemplo el exe en `C:\Program Files\`), cae a la carpeta de datos del usuario
(`%APPDATA%\FunshiEngineGL` o `~/.local/share/FunshiEngineGL`). Por eso el
mismo `args.project` funciona en cualquier dispositivo e instalación: el motor
ubica el nombre donde corresponda en cada máquina.

El flag `--project RUTA` existe a medias y hoy no hace nada —la selección por
nombre ya cubre el caso—. No conviene llenarlo con una ruta absoluta: una ruta
clavaría la ubicación específica de un dispositivo y dejaría de funcionar donde
la raíz cae en otro lado. Su destino no está definido: si se implementa,
tomaría un nombre igual que `args.project` para mantenerse portátil, y si no se
le encuentra uso puede eliminarse. Mejor no depender de él.

### Forma de la respuesta

Las respuestas siempre son un objeto JSON con `ok`, `result`, `error` y
`apiVersion`. Cuando el cliente envía un `id`, la respuesta lo devuelve en eco
para correlación:

```json
{
  "apiVersion": 1,
  "error": null,
  "id": "abc123",
  "ok": true,
  "result": { "name": "MiProyecto", "path": ".../MotorGrafico/Proyects/MiProyecto", "dimension": 2 }
}
```

Cuando una operación falla, `ok` es `false`, `result` es `null` y `error`
contiene un código estable y un mensaje:

```json
{
  "apiVersion": 1,
  "error": { "code": "invalid_project", "message": "El proyecto no existe o no tiene estructura valida" },
  "ok": false,
  "result": null
}
```

El campo `apiVersion` identifica la versión de la API. Se incrementa al
agregar funcionalidad; los campos nuevos se agregan al final.

## Comandos disponibles

| `command` | Argumentos | Resultado |
|---|---|---|
| `help` | ninguno | Lista los comandos disponibles. |
| `version` | ninguno | Devuelve la versión de la CLI. |
| `project.create` | `args.name` (string), `args.dimension` (opcional: entero `2`/`3` o string `"2d"`/`"3d"`/`"DosD"`/`"TresD"`; default `3`) | Crea el proyecto y devuelve su nombre, ruta y dimensión. |
| `project.list` | ninguno | Lista todos los proyectos descubiertos con su dimensión y validez. |
| `project.config.get` | `args.project` (string, requerido) | Devuelve la configuración persistida del proyecto (dimensión, versión, gizmo, ventanas, cámara activa). |
| `project.src.create` | `args.project` (string), `args.path` (string, ruta relativa dentro de `src/`), `args.file` (bool, opcional), `args.content` (string, opcional) | Crea un archivo (`file: true`) o directorio dentro de `src<proyecto>/`. |
| `project.dir.import` | `args.project` (string), `args.source` (string, ruta absoluta origen), `args.path` (string, ruta relativa destino en `src/`), `args.overwrite` (bool, opcional) | Importa una carpeta completa recursivamente dentro de `src<proyecto>/`. |
| `project.fs.create` | `args.project` (string, requerido), `args.name` (string, nombre del directorio) | Crea un directorio dentro del vfs `<proyecto>/fs/`. |
| `file.create` | `args.project` (string, requerido), `args.name` (string, nombre del archivo), `args.path` (string, ruta relativa dentro de `fs/`, opcional), `args.content` (string, opcional) | Crea un archivo dentro del vfs `<proyecto>/fs/`. |
| `scene.list` | `args.project` (string, requerido) | Lista las escenas (archivos `.db`/`.txt`) del proyecto. |
| `scene.objects` | `args.project` (string, requerido) | Devuelve la jerarquía completa de objetos de la escena con sus componentes. |
| `scene.object.get` | `args.project` (string, requerido), `args.id` (int) **o** `args.tag` (string) | Obtiene un objeto específico por id o tag (búsqueda recursiva). |
| `scene.hierarchy` | `args.project` (string, requerido) | Devuelve la jerarquía completa de objetos (mismo contenido que `scene.objects`). |
| `asset.import` | `args.project` (string, requerido), `args.source` (string, requerido, ruta absoluta al archivo) | Importa un modelo/textura/audio al proyecto y devuelve su ruta relativa y tipo. |
| `script.list` | `args.project` (string, requerido) | Lista los scripts del proyecto (`.cpp` y `.java` en `src<proyecto>/Scripts/`). |
| `script.compile` | `args.project` (string, requerido) | Compila **todos** los scripts (C++ y Java) del proyecto vía `ScriptRuntime`. |
| `diagnostics.list` | `args.limit` (opcional, máx 1000, default 100) | Devuelve los últimos eventos del registro de diagnósticos. |
| `diagnostics.clear` | ninguno | Borra el archivo de diagnósticos. |

## Crear proyecto con dimensión

`project.create` acepta `args.dimension`:

- **Entero**: `2` (2D) o `3` (3D)
- **String**: `"2d"`, `"2D"`, `"DosD"` o `"3d"`, `"3D"`, `"TresD"`
- **Default**: `3` (3D), para retrocompatibilidad con proyectos previos al campo

Ejemplo 2D:

```json
{"command": "project.create", "args": {"name": "MiJuego2D", "dimension": 2}}
```

## Importar assets

`asset.import` detecta el tipo por extensión y copia el archivo a la
subcarpeta correspondiente dentro de `src<proyecto>/`:

| Extensión | Tipo | Destino |
|---|---|---|
| `.obj`, `.fbx`, `.gltf`, `.glb`, `.dae`, `.blend` | model | `Modelos/` |
| `.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.hdr` | texture | `Texturas/` |
| `.wav`, `.ogg`, `.mp3`, `.flac` | audio | `Sonidos/` |

Ejemplo:

```json
{"command": "asset.import", "args": {"project": "MiProyecto", "source": "/tmp/modelo.fbx"}}
```

Respuesta:

```json
{"ok": true, "result": {"project": "MiProyecto", "path": "Modelos/modelo.fbx", "type": "model", "exists": true}}
```

## Consultar escenas y objetos

`scene.objects` (y su alias `scene.hierarchy`) carga la escena completa y
devuelve la jerarquía recursiva de cada objeto con:

- `id`, `name`, `tag`, `active`
- `transform` (posición/rotación/escala local)
- `components` (array con `typeName` y las propiedades específicas de cada tipo:
  Transform, Script, Model, Color, …)
- `children` (recursivo)

`scene.object.get` busca por `id` o por `tag` (primera coincidencia recursiva).

## Compilar scripts

`script.compile` usa `ScriptRuntime` (BackendCpp + BackendJava) y compila todos
los `.cpp` y `.java` en `src<proyecto>/Scripts/`. Devuelve un resumen con
`compiled`, `failed`, `skipped` y el detalle por archivo.

## Crear archivos y carpetas

`project.src.create` crea estructura dentro de `src<proyecto>/`:

- `args.file: false` (default) → crea un directorio (incluye los padres)
- `args.file: true` → crea un archivo con `args.content` opcional

Ejemplo archivo:

```json
{"command": "project.src.create", "args": {"project": "MiProyecto", "path": "Scripts/Enemigo.cpp", "file": true, "content": "#include ..."}}
```

Ejemplo directorio:

```json
{"command": "project.src.create", "args": {"project": "MiProyecto", "path": "Scripts/Enemigos"}}
```

`project.fs.create` y `file.create` operan sobre el vfs `<proyecto>/fs/` en vez
de `src/`: crean respectivamente un directorio y un archivo dentro de `fs/`.

## Importar una carpeta completa (recursivo)

`project.dir.import` copia una carpeta externa completa (recursivamente) dentro
de `src<proyecto>/`. Útil para importar assets por lotes (sprites, tilesets,
etc.).

Argumentos:

- `args.project` (string, requerido): nombre del proyecto destino.
- `args.source` (string, requerido): ruta absoluta de la carpeta origen.
- `args.path` (string, requerido): ruta relativa destino dentro de `src<proyecto>/`.
- `args.overwrite` (bool, opcional, default `false`): si el destino existe, permite reemplazarlo.

Ejemplo:

```json
{"command": "project.dir.import", "args": {"project": "MiProyecto", "source": "/home/usuario/Sprites", "path": "Assets/Sprites"}}
```

Respuesta:

```json
{"ok": true, "result": {"project": "MiProyecto", "source": "/home/usuario/Sprites", "destination": "Assets/Sprites", "filesCopied": 42, "overwrite": false}}
```

## Consultar diagnósticos

`diagnostics.list` devuelve los eventos del registro de diagnósticos compartido
con el editor. Admite un único filtro:

- `args.limit` (opcional): cantidad máxima de eventos a devolver, con techo en
  1000. Default 100.

Cada evento incluye `timestamp`, `level`, `module`, `message`, `project`,
`exception` y `stack`.

`diagnostics.clear` borra el archivo de diagnósticos.

## Códigos de salida

| Código | Significado |
|---:|---|
| `0` | Solicitud completada con éxito. |
| `1` | Falló una operación (crear proyecto, importar, compilar, etc.). |
| `2` | Argumentos o solicitud JSON inválidos, archivo de solicitud inaccesible o comando desconocido (error de uso). |
| `3` | El proyecto indicado (`args.project`) no existe o no tiene estructura válida. |
| `4` | Recurso no encontrado (objeto, asset, carpeta origen) o una ruta intentó escapar del directorio permitido. |
| `5` | Fallo de E/S al crear o copiar un archivo/directorio. |
