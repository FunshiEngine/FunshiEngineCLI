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

> Nota: el motor se compila desde el submódulo `extern/FunshiEngine`, que el
> CMake incluye con `add_subdirectory`; la CLI enlaza contra esa misma
> biblioteca. Cabeceras y biblioteca salen siempre del mismo commit del
> submódulo, así que no hace falta mantener un build aparte del motor para
> poder compilar la CLI. Si actualizás el submódulo
> (`git submodule update --init --recursive`), reconstruí la CLI entera.

> Nota (Windows): el build copia junto al ejecutable las DLLs de runtime que
> necesita al arrancar, tomadas del directorio del compilador (en MSYS2,
> `C:\msys64\mingw64\bin`): toolchain MinGW-w64 (`libstdc++-6`,
> `libgcc_s_seh-1`, `libwinpthread-1`), motor (`libassimp-6`, `glfw3`, las de
> Bullet) y dependencias de assimp (`libminizip-1`, `zlib1`, `libbz2-1`). En
> `build/` la CLI funciona así desde cualquier terminal (MINGW64, UCRT64,
> PowerShell, cmd) sin preparar el PATH. Si copiás el ejecutable a otra
> carpeta, llevate también esas DLLs: sin ellas el proceso termina en el
> arranque sin imprimir nada —en shells MSYS2 se ve `error while loading
> shared libraries: libassimp-6.dll: ...` y el exit code es `0xC0000135`.

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

La pantalla tiene cinco piezas:

- **Barra superior**: versión de la CLI, directorio de trabajo y proyecto
  activo.
- **Panel lateral**: los dominios — Proyectos, Escena, Scripts y
  Diagnósticos — y, debajo, el proyecto activo.
- **Panel central**: los datos reales de la sección, pedidos al núcleo
  (`project.list`, `scene.list`, `script.list`, `diagnostics.list`). Su
  cabecera lleva el número de elementos y, si no caben todos, el rango
  visible (`12-30 de 87`).
- **Caja de comandos**: una línea de entrada donde se escriben comandos del
  catálogo.
- **Barra de estado**: el desenlace de la última operación y, en la línea
  siguiente, los atajos. La línea de atajos se acorta sola cuando la terminal
  es estrecha, de modo que nunca queda cortada a media palabra.

Teclas, con el foco en el panel:

| Tecla                | Acción                                   |
|----------------------|------------------------------------------|
| `↑` / `↓`            | navegar la lista de la sección actual    |
| `RePág` / `AvPág`    | recorrer la lista de una página           |
| `Inicio` / `Fin`     | ir al primer o al último elemento        |
| `←` / `→` / `Tab`    | cambiar de sección (recarga sus datos)   |
| `Enter`              | en Proyectos: fija el proyecto activo    |
| `r`                  | recargar la sección actual               |
| `/`                  | enfocar la caja de comandos              |
| `?`                  | abrir el catálogo de comandos            |
| `q`, `Esc`, `Ctrl+C` | salir                                    |

Teclas, con el foco en la caja de comandos:

| Tecla                | Acción                                   |
|----------------------|------------------------------------------|
| `Enter`              | ejecutar el comando escrito              |
| `Esc`                | devolver el foco al panel                |
| `↑` / `↓`            | recorrer el historial de lo escrito      |
| `Ctrl+C`             | salir                                    |

El resto de las teclas se escriben en la caja: ahí `q` es una letra, no una
salida.

### Escribir comandos

La caja de comandos usa la misma sintaxis que las solicitudes JSON: el nombre
del comando y, detrás, sus argumentos en forma `nombre=valor`.

```
project.list
project.create name=MiJuego dimension=2
scene.object.get project=MiJuego id=12
```

Un valor numérico sin comillas se envía como número; entrecomillado (`"2"`, o
`'un texto con espacios'`) se envía como texto. La línea se parte en palabras
respetando las comillas, así que un valor entrecomillado puede contener
espacios. Un comando desconocido, o con argumentos que no encajan, deja su
código de error en la barra de estado sin interrumpir la sesión.

### Catálogo de comandos

`?` abre un panel con la lista completa de comandos del catálogo y un resumen
de las teclas. Cualquier tecla lo cierra.

Sin proyecto activo, las secciones Escena y Scripts piden elegir uno en
Proyectos (`Enter` sobre la fila).

### Requisitos de la terminal

En Windows, la TUI funciona en cualquier consola nativa (cmd, PowerShell,
Windows Terminal o mintty con ConPTY), donde las teclas se leen con la API de
consola. En un pty de MSYS2 o Git Bash sin ConPTY esa API no está disponible,
así que la CLI lee las teclas directamente de la entrada y aplica el modo
crudo con `stty` (que debe estar en el PATH, como en cualquier instalación de
MSYS2). En POSIX la entrada la resuelve el propio FTXUI. Además, en Windows
la TUI necesita que el ejecutable arranque con sus DLLs de runtime (ver
«Compilar»): el build las copia junto a él, de modo que desde `build/` no hay
que preparar nada; si el ejecutable se mueve a otra carpeta, hay que
llevarse las DLLs con él.

Sin una terminal interactiva —stdin redirigido desde un archivo o una
tubería, o lanzado desde un programa sin terminal— la TUI no tendría forma de
recibir teclas y se niega a arrancar:

```console
$ FunshiCLI --tui < archivo.json
{"apiVersion":1,"error":{"code":"tui_terminal_unavailable", ...}, ...}
$ echo $?
2
```

En la consola nativa el tamaño de la terminal se toma del estado de la
consola. El ratón llega solo en POSIX: FTXUI activa ahí el modo de reporte del
ratón y la terminal manda los eventos de puntero. En Windows no llega, porque
FTXUI no implementa los eventos de ratón de la consola de Windows. En la vía
sin consola de Windows tampoco: ahí el ratón queda desactivado a propósito,
porque el lector de respaldo no traduce esas secuencias.
Limitaciones de la vía sin consola de Windows: el tamaño de la terminal se
detecta una sola vez al arrancar (un cambio posterior no se refleja) y las
teclas pulsadas durante los primeros segundos de arranque pueden perderse,
porque las órdenes `stty` de la puesta en marcha corren como procesos que
heredan la entrada. Conviene esperar a que la pantalla se pinte antes de
navegar.

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

`scene.script.compile` compila y carga el script de un objeto específico (si se
pasa `args.id`) o todos los scripts del proyecto (sin `id`):

```json
{"command": "scene.script.compile", "args": {"project": "MiProyecto", "id": 6}}
```

`scene.script.reload` fuerza recompilar y recargar el script de un objeto:

```json
{"command": "scene.script.reload", "args": {"project": "MiProyecto", "id": 6}}
```

## Crear y gestionar objetos de escena

`scene.object.create` crea un nuevo GameObject en la escena, con nombre y padre opcional:

```json
{"command": "scene.object.create", "args": {"project": "MiProyecto", "name": "Enemigo", "parentId": 0}}
```

Respuesta: `{"id": 9, "name": "Enemigo", "parentId": 0, "created": true}`.

`scene.object.clone` clona un objeto existente con todos sus componentes (Script, Model, Color, AudioSource, Collider, RigidBody):

```json
{"command": "scene.object.clone", "args": {"project": "MiProyecto", "id": 6, "parentId": 0}}
```

Respuesta: `{"id": 10, "name": "Enemigo", "parentId": 0, "cloned": true}`.

`scene.object.removecomponent` quita un componente de un objeto por tipo:

```json
{"command": "scene.object.removecomponent", "args": {"project": "MiProyecto", "id": 6, "componentType": "Collider"}}
```

Respuesta: `{"componentType": "Collider", "id": 6, "removed": true}`.

`scene.object.unparent` desvincula un objeto de su padre (lo vuelve raíz):

```json
{"command": "scene.object.unparent", "args": {"project": "MiProyecto", "id": 6}}
```

Respuesta: `{"id": 6, "unparented": true}`.

`scene.object.settag` cambia la etiqueta de un objeto:

```json
{"command": "scene.object.settag", "args": {"project": "MiProyecto", "id": 6, "tag": "Enemigo"}}
```

Respuesta: `{"id": 6, "tag": "Enemigo", "tagged": true}`.

`scene.object.setactive` activa o desactiva un objeto:

```json
{"command": "scene.object.setactive", "args": {"project": "MiProyecto", "id": 6, "active": false}}
```

Respuesta: `{"id": 6, "active": false, "activated": true}`.

`scene.object.components` lista los componentes de un objeto con sus tipos:

```json
{"command": "scene.object.components", "args": {"project": "MiProyecto", "id": 6}}
```

Respuesta: `{"id": 6, "components": [{"type": "Transform"}, {"type": "Script"}]}`.

`scene.object.findbytag` busca objetos por etiqueta:

```json
{"command": "scene.object.findbytag", "args": {"project": "MiProyecto", "tag": "Enemigo"}}
```

Respuesta: `{"tag": "Enemigo", "objects": [{"id": 6, "name": "Enemigo", "tag": "Enemigo", "active": true}], "count": 1}`.

`scene.object.findbyname` busca objetos por nombre:

```json
{"command": "scene.object.findbyname", "args": {"project": "MiProyecto", "name": "Enemigo"}}
```

Respuesta: `{"name": "Enemigo", "objects": [{"id": 6, "name": "Enemigo", "tag": "Enemigo", "active": true}], "count": 1}`.

`scene.object.getglobaltransform` devuelve el transform global (posición, rotación, escala) compuesto con la jerarquía de padres:

```json
{"command": "scene.object.getglobaltransform", "args": {"project": "MiProyecto", "id": 6}}
```

Respuesta: `{"id": 6, "position": [1.0, 2.0, 3.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]}`.

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

## Validar extensiones de archivo (assets)

`asset.validar` valida si una extensión de archivo es soportada y devuelve su tipo (model, texture, audio):

```json
{"command": "asset.validar", "args": {"path": "modelo.fbx"}}
```

Respuesta: `{"extension": "fbx", "path": "modelo.fbx", "supported": true, "type": "model"}`.

`asset.extensions` lista todas las extensiones soportadas agrupadas por tipo:

```json
{"command": "asset.extensions", "args": {}}
```

Respuesta: `{"model": ["obj", "fbx", ...], "texture": ["png", "jpg", ...], "audio": ["wav", "ogg", ...]}`.

## Deshacer y rehacer (undo/redo)

`scene.undo` deshace la última operación registrada en el historial de la escena (settag, setactive, setparent, unparent, create, clone, removecomponent, addcomponent). El historial se persiste en `<proyecto>/Memory/undo_history.json`.

```json
{"command": "scene.undo", "args": {"project": "MiProyecto"}}
```

`scene.redo` rehace la última operación deshecha:

```json
{"command": "scene.redo", "args": {"project": "MiProyecto"}}
```

## Guardado automático (dirty tracking)

`scene.autosave` guarda todos los objetos marcados como dirty (modificados desde la última carga/guardado) y los marca como limpios. Los comandos de modificación (settag, setactive, addcomponent, removecomponent, setparent, unparent, create, clone) marcan automáticamente los objetos afectados como dirty. El dirty flag se persiste en el binario de cada objeto.

```json
{"command": "scene.autosave", "args": {"project": "MiProyecto"}}
```

Respuesta: `{"saved": 1, "total": 4, "autosaved": true}`.

## Establecer campo de script

`scene.object.setfield` establece un campo reflejado de un componente Script del objeto, buscándolo por nombre. El valor se envía como `{"value": {"v": <valor>}}`:

```json
{"command": "scene.object.setfield", "args": {"project": "MiProyecto", "id": 6, "field": "vida", "value": {"v": 100}}}
```

Soporta tipos: int, float, double, bool, string, vec3.

## Códigos de salida

| Código | Significado |
|---:|---|
| `0` | Solicitud completada con éxito. |
| `1` | Falló una operación (crear proyecto, importar, compilar, etc.). |
| `2` | Argumentos o solicitud JSON inválidos, archivo de solicitud inaccesible o comando desconocido (error de uso). |
| `3` | El proyecto indicado (`args.project`) no existe o no tiene estructura válida. |
| `4` | Recurso no encontrado (objeto, asset, carpeta origen) o una ruta intentó escapar del directorio permitido. |
| `5` | Fallo de E/S al crear o copiar un archivo/directorio. |
