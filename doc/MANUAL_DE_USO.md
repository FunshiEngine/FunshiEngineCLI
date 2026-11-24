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

La pantalla tiene **seis pestañas** en el panel lateral (navegables con `←`/`→`/`Tab`):

1. **Proyectos** — lista y selección de proyecto activo
2. **Escena** — objetos, jerarquía, componentes
3. **Scripts** — listado y compilación
4. **Diagnósticos** — registro de eventos
5. **Chat Silvy** — agente conversacional unificado
6. **Modelos HF** — explorador nativo de Hugging Face

Cada pestaña tiene:

- **Barra superior**: versión de la CLI, directorio de trabajo y proyecto
  activo. En pestañas 5 y 6 muestra también el **estado LLM**:
  - `🟢 LOCAL: <modelo>` — servidor local corriendo (llama.cpp)
  - `🔵 REMOTE` — endpoint remoto configurado (Ollama, LM Studio, etc.)
  - (sin indicador) — modo solo CLI
- **Panel lateral**: lista de secciones + proyecto activo
- **Panel central**: datos de la sección (o chat / buscador de modelos)
- **Caja de comandos** (pie): **oculta en Chat Silvy y Modelos HF**;
  en las otras 4 pestañas permite escribir comandos del catálogo (`/project.list`, etc.)
- **Barra de estado**: desenlace de la última operación + atajos contextuales

### Teclas globales (panel lateral con foco)

| Tecla                | Acción                                   |
|----------------------|------------------------------------------|
| `↑` / `↓`            | navegar la lista de la sección actual    |
| `RePág` / `AvPág`    | recorrer la lista de una página           |
| `Inicio` / `Fin`     | ir al primer o al último elemento        |
| `←` / `→` / `Tab`    | cambiar de sección (recarga sus datos)   |
| `Enter`              | en Proyectos: fija el proyecto activo    |
| `r`                  | recargar la sección actual               |
| `/`                  | enfoca la caja de comandos (pestañas 1-4) o la caja de búsqueda/chat (pestañas 5-6) |
| `?`                  | ayuda contextual                         |
| `q` / `Esc` / `Ctrl+C` | salir de la TUI                         |

### Pestaña 5 — Chat Silvy

Agente conversacional que unifica **comandos CLI** y **lenguaje natural (LLM)**.

**Entrada**: caja de chat en el panel central (enfocada con `/`).

| Escribe | Comportamiento |
|---|---|
| `/project.list` | **CLI directo** — ejecuta el comando del catálogo al instante |
| `crear proyecto MiJuego 2d` | **LLM** — el modelo razona, elige tool `project.create` y la ejecuta |
| `listar escenas del proyecto activo` | **LLM** — encadena `scene.list` con el proyecto actual |
| `diagnosticos` | **LLM** — ejecuta `diagnostics.list` |

**Indicador de modo** (cabecera del panel central):
- `[CLI]` — sin LLM configurado, o último comando fue `/...`
- `[LOCAL: modelo]` — LLM local activo (ej. `[LOCAL: Qwen2.5-7B-Instruct-Q4_K_M.gguf]`)
- `[REMOTE]` — endpoint remoto activo

**Teclas en Chat Silvy**:
| Tecla | Acción |
|---|---|
| `/` | Enfoca la caja de chat |
| `↑` / `↓` | Historial de órdenes propias |
| `RePág` / `AvPág`, `Ctrl+↑` / `Ctrl+↓`, rueda del ratón | Recorren la conversación |
| `↑` / `↓`, `Inicio` / `Fin` (chat sin foco, tras `Esc`) | Recorren la conversación, línea a línea o hasta un extremo |
| `Enter` | Ejecuta la orden escrita |
| `Ctrl+N` | Sesión nueva |
| `Ctrl+←` / `Ctrl+→` (o `Ctrl+P` / `Ctrl+O`) | Sesión anterior / siguiente |
| `Ctrl+X` | Cancela la respuesta en curso de esta sesión |
| Clic en una acción | Muestra (o vuelve a ocultar) sus argumentos y su resultado completos |
| Clic en un subagente | Abre su vista: la tarea que recibió y todo lo que hizo |
| `Esc` | En la vista de un subagente, vuelve a la conversación; si no, al panel lateral |

#### Monitorear lo que hace Silvy

La conversación muestra, en el orden en que ocurren, todos los pasos de cada
pedido:

| Línea | Qué es |
|---|---|
| `· documentación recuperada: …` | Comandos y guías que el RAG le pasó al modelo para este pedido |
| `· ronda 2 · 3.4 s (1.er token 1.2 s) · 840 → 56 tokens` | Cada consulta al modelo: mientras espera dice «esperando al modelo»; al terminar, el tiempo y los tokens de entrada → salida |
| `Piensa …` | El razonamiento del modelo: lo que escribió antes de usar una herramienta y, en modelos que razonan (Qwen3, DeepSeek-R1…), su razonamiento interno, que no se guarda en el historial |
| `Acción ⟳ / ✔ / ✘ …` | Cada herramienta o comando, con sus argumentos (en amarillo mientras corre; verde si salió bien, rojo si falló) y las primeras líneas del resultado. Con un clic se ven completos |
| `┌ Jugador.java · Java (.java) · Script Java del motor · 24 líneas` | El archivo que escribe una acción: nombre, lenguaje y extensión, cómo lo trata el motor, ruta, el **resumen** que escribió Silvy sobre su contenido, un aviso si el lenguaje no es un script que el motor ejecute (solo `.java` y `.cpp`) y el código completo (más de 60 líneas se recortan: un clic lo muestra entero) |
| `Subag. ⟳ 1/2 título · perfil … · 3 acciones` | Un subagente: su avance y la acción que está haciendo ahora; al terminar, el resultado de su informe |
| `· Cancelado`, `· Error del modelo…` | Cortes y errores |

Mientras miras más arriba, lo que llega no mueve la vista; la cabecera avisa
«↓ más abajo» y `AvPág`, la rueda o `Fin` vuelven al final. Al enviar una
orden la vista vuelve al final.

**Vista de un subagente**: con un clic en su línea, o con `/sub N`, el panel
pasa a mostrar su trabajo: primero la **tarea** que recibió (título, perfil de
herramientas, instrucciones y contexto), después su razonamiento, sus acciones
con resultado y su informe final. Si todavía trabaja, se ve en vivo. `Esc` o
`/volver` regresan a la conversación. La vista de cada subagente se guarda con
la sesión.

| Orden | Acción |
|---|---|
| `/subs` | Lista los subagentes de esta sesión |
| `/sub N` | Abre la vista del subagente N |
| `/volver` | Regresa a la conversación |

#### Sesiones

Cada sesión es una conversación independiente con Silvy: tiene su propio
historial (el modelo solo ve el de esa sesión), su proyecto activo y sus
mensajes. Las pestañas bajo la cabecera muestran las sesiones; `⟳` marca las
que están procesando. **Varias sesiones pueden trabajar a la vez**: mientras
una procesa, puedes cambiar a otra y darle otra orden.

Se guardan solas en `<carpeta de config>/sesiones/<id>.json` y vuelven al
abrir la TUI. La primera orden le pone nombre a la sesión.

Órdenes de sesión (sin punto; las que llevan punto, como `/project.list`, siguen
siendo comandos de la CLI):

| Orden | Acción |
|---|---|
| `/nueva [título]` | Abre una sesión nueva |
| `/sesiones` | Lista las sesiones (● la actual) |
| `/sesion N` | Cambia a la sesión N |
| `/renombrar título` | Cambia el título |
| `/limpiar` | Vacía la conversación de esta sesión |
| `/borrar` | Borra la sesión (pide `/borrar confirmar`) |
| `/cancelar` | Corta la respuesta en curso |
| `/ayuda` | Muestra estas órdenes |

#### Tareas

Cada mensaje que le das a Silvy es una **tarea**, y ella misma la abre y la
cierra:

- **Al empezar**, con la tarea anterior a la vista, escribe en una línea lo
  que debe hacer ahora; en el chat aparece como la nota `· Tarea: …`. Así
  pedidos como «ahora ponle física» o «agrégale un método» se entienden sobre
  lo que acaba de hacer.
- **Al terminar**, escribe lo que hizo (una línea por acción, con ids y rutas)
  y un resumen de qué había que hacer y cómo quedó (`· Resumen de la tarea: …`).
  Si no lo escribe en el formato esperado, o si ninguna herramienta salió
  bien, la lista se arma con las herramientas que ejecutó (así no queda
  registrado algo que no pasó).

Lo que recibe el modelo en cada pedido, además del historial y la
documentación, es esto (markdown en texto plano, el formato que mejor
respetan los modelos chicos):

```
## Tarea anterior
Lo que hice:
- Creé el objeto Cubo (id 1) en el proyecto Demo
Resumen: Había que crear Cubo; quedó con id 1.

## Tarea actual
ahora ponle un RigidBody
Ahora: agregar un RigidBody al objeto Cubo (id 1).
```

Cada tarea suma dos llamadas cortas al modelo (el objetivo y el cierre). La
tarea anterior se guarda con la sesión y `/limpiar` la borra.

#### Qué sabe Silvy de los comandos (RAG)

Antes de cada pedido, Silvy busca en su base de conocimiento los comandos y
guías relevantes y se los pasa al modelo junto con el pedido. Así el modelo
conoce el nombre exacto, los argumentos y un ejemplo de cada comando que
necesita, sin tener los 48 en el contexto. Además puede:

- **`buscar_comandos`**: buscar documentación por su cuenta;
- **`ejecutar_comando`**: usar cualquier comando del catálogo, también los que
  no tienen herramienta propia (mover objetos, agregar componentes, listar
  `src/`…). Por seguridad no puede usar `project.src.delete`,
  `diagnostics.clear`, `cli.config.set` ni `agente.procesar`: esos los
  ejecutas tú con `/comando`.

La base incluye la ayuda de todos los comandos y guías de varios pasos
(crear un objeto con modelo 3D, dar física, agregar un script, sonido,
jerarquía, deshacer, errores de compilación…). **Puedes agregar tu propio
conocimiento**: archivos `.md` en `<carpeta de config>/conocimiento/`. Cada
encabezado `##` es un documento (por ejemplo, las convenciones de tu juego:
«todos los enemigos llevan el tag Malo y van bajo el objeto Horda»). Se
recargan solos al cambiar.

La base trae también la guía **Lenguajes, extensiones y archivos del motor**:
los lenguajes de programación y formatos con sus extensiones, los archivos
que reconoce el motor (se arma con la clasificación del propio motor, así que
sigue sus cambios), que los scripts del motor son solo Java (`.java`) y C++
(`.cpp`), y que el motor reconoce `.fnep` como archivo de proyecto pero
todavía no crea ni abre esos archivos (un proyecto es su carpeta). Al
escribir un archivo, Silvy elige la extensión del lenguaje y agrega un
`resumen` de su contenido, que se ve en el chat y que usa en su respuesta.

Silvy no reemplaza un archivo de `src/` que ya existe salvo que lo pida
explícitamente (`"sobrescribir": true`, que usa solo si le pediste cambiar
ese archivo) o que el contenido nuevo conserve todas las líneas del anterior
(por ejemplo, al agregar un método): si se perdería algo, la llamada falla y
el archivo queda como estaba. Desde
la caja de comandos, `project.src.create` sigue reemplazando el archivo como
siempre.

#### Documentos del usuario (RAG)

Silvy puede consultar documentos tuyos: el diseño del juego, reglas,
especificaciones, apuntes. Déjalos en la carpeta de documentos,
`<carpeta de config>/documentos/` (`.agentes/documentos/`, que **no se sube a
git**; se crea sola con un `LEEME.txt`). Formatos: PDF, Word (`.docx`),
OpenDocument (`.odt`, `.odp`, `.ods`), PowerPoint (`.pptx`), Excel (`.xlsx`),
EPUB, HTML, Markdown, texto, CSV y código.

- **Se indexan solos al abrir la TUI** (en segundo plano; el avance se ve en la
  barra de estado) y con `/docs actualizar`. Solo se procesa lo nuevo o
  cambiado; lo que borras sale de la base.
- **Embeddings**: la primera vez que hay documentos se descarga
  `Qwen3-Embedding-0.6B-Q8_0.gguf` (639 MB, de `Qwen/Qwen3-Embedding-0.6B-GGUF`,
  verificado con SHA256) a la carpeta de modelos y se levanta un segundo
  `llama-server` con `--embedding` en el puerto 11500. Convive con el del
  chat y se detiene al salir de la TUI. Sin documentos no se descarga ni se
  levanta nada.
- **Base local propia**: `<carpeta de config>/documentos.fvdb`, un archivo
  binario con los fragmentos de texto (~900 caracteres, con su página) y sus
  vectores. La búsqueda compara significado (similitud coseno): «¿cuándo
  consigue el salto doble?» encuentra «las botas del viento en el nivel 2».
  Sin servidor de embeddings, la búsqueda es por palabras.
- **Cómo lo usa Silvy**: si un fragmento se parece lo suficiente al pedido, se
  le pasa junto con el pedido (nota `· documentos del usuario: diseno.pdf p.2`);
  además tiene la herramienta `buscar_documentos` y debe decir de qué archivo
  sale lo que responde.
- **PDF**: se usa `pdftotext` (poppler) si está instalado; si no, un extractor
  propio para PDFs comunes. Los PDF escaneados (imágenes) no tienen texto.

| Orden | Acción |
|---|---|
| `/docs` | Carpeta, archivos, fragmentos, modelo y última actualización |
| `/docs actualizar` | Indexa ahora |
| `/docs buscar <texto>` | Busca en los documentos y muestra los fragmentos con su archivo y página |

En `llm-config.json`: `documentosDir` (otra carpeta), `embeddingsUrl` (un
servidor de embeddings OpenAI-compatible propio, en lugar del local) y
`embeddingsModelo` (otro GGUF de embeddings).

#### Subagentes

Para pedidos con varias partes independientes, Silvy puede delegar en
**subagentes** (`lanzar_subagentes`, hasta 4 por vez). Cada subagente hace una
sola tarea con su propio contexto, solo con las herramientas de su perfil, y
devuelve un informe:

| Perfil | Puede |
|---|---|
| `lectura` | consultar (listar, obtener, buscar); no modifica nada |
| `escena` | además, modificar la escena (objetos, transform, componentes, jerarquía) |
| `archivos` | además, archivos de `src/`, vfs `fs/` e importar assets |
| `scripts` | además, crear scripts, compilar, recargar y cambiar campos |
| `completo` | todo lo que puede el agente principal |

Los subagentes no pueden volver a delegar, y Silvy delega como mucho 2 tandas
por pedido (una, y otra con los resultados de la primera); el resumen marca
`✔` completado, `◐` parcial o `✘` fallido según el informe. En el chat cada uno tiene una
línea con su avance; un clic en ella (o `/sub N`) muestra su tarea y todo su
trabajo (ver «Monitorear lo que hace Silvy»). Las reglas de cuándo
delegar están en las instrucciones de Silvy: delega partes independientes que
requieren varias herramientas o revisar mucha información; no delega una
sola acción ni pasos que dependen entre sí. Con servidor local corren de a uno
(el servidor atiende un pedido por vez); con una API remota, hasta 3 a la vez
(`subagentesParalelos` en `llm-config.json`, 0 = automático).

> Modelo recomendado: la delegación pide al modelo armar tareas autocontenidas
> en JSON. Phi-4-mini (3.8B, sin herramientas nativas) lo hace de forma poco
> confiable; con un modelo de 7B o más que admita herramientas nativas (por
> ejemplo Qwen2.5-7B-Instruct en GGUF) funciona mucho mejor.

### Pestaña 6 — Modelos HF (Hugging Face)

Explorador nativo de modelos GGUF en huggingface.co — **sin depender de FunshiEngineAI**.

La pestaña tiene dos vistas; `Tab` alterna entre ellas:

- **Descargados** (vista inicial): los GGUF que ya tienes en disco (`modelsDir`, `localInferenceDir/models` y `~/.agentes/local-inference/models`), con tamaño y carpeta. `Enter` usa el modelo directamente, sin pasar por Hugging Face.
  - `●` marca el modelo **activo**.
  - `✗ incompleto` marca un archivo truncado (una descarga cortada): se detecta leyendo la cabecera GGUF y no se puede seleccionar. Bórralo o vuelve a descargarlo.
- **Buscar en Hugging Face**: búsqueda de modelos GGUF; cada resultado muestra cuántas variantes (archivos) ofrece.

**Flujo de descarga auto-guiado**:
1. `Tab` → caja de búsqueda → escribe query (ej: `qwen2.5 gguf`, `phi-4-mini`) → `Enter`
2. Lista de modelos (ordenados por descargas, con su número de variantes) → `↑`/`↓` navegar → `Enter` selecciona
3. **Tabla de variantes** del modelo seleccionado:
   | Archivo | Tamaño (GB) | Quantization | SHA256 |
   |---|---|---|---|
   | `Qwen2.5-7B-Instruct-Q4_K_M.gguf` | 4.37 | Q4_K_M | `abc123...` |
   | `Qwen2.5-7B-Instruct-Q5_K_M.gguf` | 5.12 | Q5_K_M | `def456...` |
4. `↑`/`↓` navegar variantes (las que ya tienes muestran `✓ descargado`) → `Enter` **selecciona y lanza auto-configuración**:
   - Si el GGUF ya está en disco con el tamaño exacto publicado, **no se vuelve a descargar**
   - Si no, lo descarga con progreso real a `<archivo>.part` (se **reanuda** si se corta) y verifica SHA256 antes de reemplazar el archivo final
   - Usa el **binario oficial de `llama-server`** para tu SO/arquitectura (releases de `ggml-org/llama.cpp`), sin clonar ni compilar
   - **Levanta `llama-server`** en puerto libre (`11434` → `11435` → ...) con el contexto configurado (`contextSize`, 32K por defecto), un solo slot y reutilización de la caché de prompts
   - Si la plantilla de chat del modelo no admite herramientas nativas (p. ej. Phi-4-mini), Silvy usa automáticamente el modo de herramientas por prompt
   - **Health check** `/v1/models` hasta que responde
   - **Configura Silvy** con `LlmConfig` LOCAL (baseUrl del server, modelo, temp=0.7)
   - Guarda config en `.agentes/llm-config.json` (persistente)
5. Vuelta automática a **Chat Silvy** con indicador `[LOCAL: variante]`

**Teclas en Modelos HF**:
| Tecla | Acción |
|---|---|
| `Tab` | Alterna entre **Descargados** y **Buscar en Hugging Face** |
| `/` | Vuelve a enfocar el panel (si saliste con `Esc`) |
| `↑` / `↓` | Navegar la lista visible |
| `RePág` / `AvPág` | Scroll página |
| `Enter` | En Descargados: usa el modelo. En búsqueda: ejecuta query / selecciona modelo. En variantes: auto-config completa |
| `l` | Salta a Descargados (fuera de la caja de búsqueda) |
| `r` | Recarga la lista actual (o reintenta tras un error) |
| `Esc` | Sube un nivel (variantes → resultados → caja); en Descargados sale al panel lateral |

**Cache local**: `.agentes/hf-cache.json` (TTL 24h) — búsquedas y metadata de modelos.

### Pestaña 7 — Configuración

| Grupo | Qué se configura | Cómo |
|---|---|---|
| Rutas y modelo | Datos/proyectos, inferencia, modelos, caché de Hugging Face, `llama-server`, contexto | `Enter` prellena `cli.config.set clave="valor"` en la caja de comandos |
| Interfaz | Idioma: Español, English, Português, Français, Italiano, Deutsch | `Enter` pasa al siguiente. **Por ahora solo se guarda**: los textos siguen en español |
| Colores | Cada color de la TUI: fondos (general, paneles, barras, selección, avisos), texto, texto tenue, acento, éxito, advertencia y error | Clic o `Enter` abre el **selector de color** |
| | Restablecer todos los colores | `Enter` |
| Documentos para Silvy | Carpeta, estado de la base y «Actualizar ahora» | `Enter` sobre «Actualizar» indexa en el momento |

**Selector de color**: una ventana con un cuadro (saturación de izquierda a
derecha, brillo de abajo hacia arriba) con un punto, y debajo un slider de
tono; juntos cubren todos los colores. Se usa con el ratón: presiona sobre
el cuadro o el slider y arrastra; la rueda cambia el tono. El color se aplica
en vivo a toda la TUI mientras lo mueves.

| Tecla | En el selector |
|---|---|
| Flechas | Mueven el punto |
| `[` / `]` (o `RePág`/`AvPág`) | Cambian el tono |
| `Enter` | Aplica y guarda |
| `Esc` | Cancela y devuelve el color original |
| `x` | Cierra y deja `tui.color clave=` en la caja de comandos para escribir el valor |

Cada color muestra una muestra, su valor, dónde se ve y si está «por
defecto» o «personalizado». Las órdenes `tui.color <clave>=<color>` y
`tui.idioma <código>` también se pueden escribir directamente en la caja de
comandos. Se guardan en `<carpeta de config>/tui.json` (solo los colores
cambiados) y se aplican al abrir la TUI.

### Modo Chat directo (`--chat`)

`FunshiCLI --chat` abre la TUI directamente en la pestaña **Chat Silvy** (equivalente a `--tui` + `→→→→→`).

```bash
FunshiCLI --chat
# Escribe: crear proyecto Test 2d
# Escribe: /project.list
```

## Configuración de rutas

La CLI permite consultar y modificar las rutas/directorios usados para datos/proyectos y para inferencia/modelos locales, persistiendo los cambios en `.agentes/llm-config.json` (en el directorio de trabajo):

```bash
# Ver rutas configuradas y efectivas
printf '{"command":"cli.config.get","args":{}}' | FunshiCLI --request

# Modificar rutas (cadena vacía = valor por defecto)
printf '{"command":"cli.config.set","args":{"projectsDataDir":"/ruta/datos","modelsDir":"/ruta/modelos","hfCacheDir":"/ruta/cache-hf","localInferenceDir":"/ruta/inferencia"}}' | FunshiCLI --request
```

Claves admitidas por `cli.config.set`: `projectsDataDir`, `localInferenceDir`, `modelsDir`, `hfCacheDir`, `llamaServerPath` y `contextSize` (entero, tokens de contexto del modelo local; por defecto `32768`). `llamaRepoDir` y `gitBin` se aceptan por compatibilidad pero ya no se usan.

**Dónde se guarda la configuración** (`configPath` en `cli.config.get`), en este orden:
1. `$FUNSHI_CONFIG_DIR/llm-config.json`, si la variable está definida.
2. `.agentes/llm-config.json` en la carpeta actual **o en cualquiera de sus padres** (da igual abrir la CLI desde la raíz del repo o desde `build/`).
3. Configuración global por usuario: `~/.agentes/llm-config.json` (Linux/macOS) o `%APPDATA%\FunshiEngine\llm-config.json` (Windows).

Las rutas relativas se resuelven respecto de la carpeta que contiene `.agentes/`, no de la carpeta actual.

**Métricas de rendimiento**: con `FUNSHI_PERF=1` se registran en `metricas.jsonl` (junto a la configuración) el tiempo hasta el primer token, tokens/s, tamaño de cada petición al modelo y el tiempo de dibujo del chat.

### llama-server

Al abrir la TUI se comprueba en segundo plano la última build publicada en
`github.com/ggml-org/llama.cpp/releases` y, si difiere de la instalada, se
descarga el binario de tu plataforma en `<localInferenceDir>/llama-bin/<tag>/`
(Linux `ubuntu-x64/arm64`, macOS `x64/arm64`, Windows `win-cpu-x64/arm64`).
Sin conexión se sigue usando la versión instalada; la barra superior muestra el
estado. Para usar un binario propio, define `llamaServerPath`.

Los cambios de `projectsDataDir` (ubicación de datos/proyectos del motor) pueden requerir reiniciar la CLI para aplicarse por completo en la sesión actual.

`projectsDataDir` es la carpeta **que contiene** `Proyects/` (la raíz
`FunshiEngine-DATA`). Si apunta a la propia `Proyects/`, o a una carpeta vacía
junto a ella (por ejemplo `FunshiEngine-DATA/Projects`), la CLI usa la raíz
real: `cli.config.get` muestra en `effectivePaths` la ruta que se usa.

## Integración con el agente

`agente.procesar` entrega un texto a Silvy y devuelve la respuesta junto con el
rastro de Harness/Auditoria. Permite comprobar la integración CLI↔agente sin
abrir la TUI:

```bash
printf '{"command":"agente.procesar","args":{"text":"listar proyectos"}}' | FunshiCLI --request
printf '{"command":"agente.procesar","args":{"text":"/version"}}' | FunshiCLI --request
```

El texto se interpreta como comando CLI cuando empieza con `/`, cuando es una
solicitud JSON o cuando coincide con una orden en lenguaje natural soportada.
El resultado incluye `fueComandoCli`, `comandoOk`, la `respuesta` interna y un
objeto `auditoria` con el último comando registrado. Si no hay LLM configurado
y el texto no se interpreta como comando, `error` es `true` y se devuelve la
explicación en `texto`; no se intenta ninguna llamada de red.

## Ayuda y versión
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
{"apiVersion":2,"error":{"code":"tui_terminal_unavailable", ...}, ...}
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

### Seleccionar y copiar texto con el ratón

En el panel central (listas, Chat Silvy, Modelos, ayuda) se puede copiar
cualquier texto visible. Lo resaltado es exactamente lo que se copia.

| Gesto | Selecciona |
|---|---|
| Arrastrar | Desde donde se presiona hasta donde se suelta, como en una terminal |
| Doble clic | La palabra bajo el cursor |
| Triple clic | La línea entera (sin bordes ni sangría) |

Al soltar el botón el texto va al portapapeles y un aviso arriba a la derecha
dice cuántos caracteres se copiaron y con qué método, o por qué no se pudo
copiar. Cualquier tecla quita el resaltado.

Método usado, en orden:

| Entorno | Método |
|---|---|
| Windows | API Win32 (texto Unicode: acentos correctos) |
| macOS | `pbcopy` |
| Linux con Wayland | `wl-copy` (paquete `wl-clipboard`) |
| Linux con X11 | `xclip` o `xsel` |
| Dentro de tmux | `tmux load-buffer -w` (tmux 3.2 o superior) |
| Sin ninguno (SSH, consola sin X) | **OSC 52**: la propia terminal copia |

OSC 52 funciona en la mayoría de las terminales modernas (kitty, Alacritty,
WezTerm, foot, iTerm2, Windows Terminal, GNOME Terminal 3.50+). Dentro de tmux
hace falta `set -g set-clipboard on` o `allow-passthrough on`. Como la CLI no
puede confirmar que la terminal lo aceptó, el aviso dice «Enviado a la
terminal». El límite es de unos 74 KB de texto.

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
  "apiVersion": 2,
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
  "apiVersion": 2,
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
| `cli.config.get` | ninguno | Devuelve las rutas configuradas y efectivas. |
| `cli.config.set` | `args.<ruta>` (string; una o más de `projectsDataDir`, `localInferenceDir`, `modelsDir`, `hfCacheDir`, `llamaServerPath`, `llamaRepoDir`, `gitBin`) | Persiste rutas y las aplica al entorno. |
| `agente.procesar` | `args.text` (string, requerido) | Entrega el texto a Silvy y devuelve la respuesta interna y el rastro de auditoría. |

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
