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
| `Nucleo/` | Modelo de petición/respuesta, catálogo cerrado de comandos, dispatch, ayuda de cada comando y sintaxis de línea | `Solicitud`, `ResultadoComando`, `ComandoCLI`, `ejecutarSolicitud()`, `ayudaComandos()`, `construirArgumentos()` | `Comandos/` |
| `Validacion/` | Validación de argumentos por tabla | `validarArgumentos()`, `argumentoProyecto()` | — |
| `Proyectos/` | Resolver un proyecto por nombre o ruta | `resolverProyectoPorNombre()` | motor |
| `Escena/` | Armazón RAII de escena y serialización a JSON | `ContextoEscena`, `sceneHierarchyJson()` | motor |
| `Comandos/` | Un manejador por comando (`ResultadoComando f(const json&)`); `ComandosBorrado`: borrar objetos (con copia para deshacer), scripts y assets quitando sus referencias, y listar assets; `ComandosPapelera`: lo borrado va a `<proyecto>/Memory/papelera` con un índice (`indice.json`) y se recupera con sus referencias | `escena*`, `proyecto*`, `script*`, `escenaObjetoBorrar()`, `quitarReferenciasA()`, `moverAPapelera()`, `papeleraRestaurar()`… | Validacion, Proyectos, Escena, motor |
| `Agente/` | Silvy (enrutado y armado de cada pedido), Harness (ejecución auditada y serializada), Auditoría | `Silvy::procesar(texto, SesionAgente&, onEvent)`, `Harness::ejecutar()` | Nucleo, Agente/LLM |
| `Agente/Conocimiento` | RAG: índice BM25 sobre la ayuda de los comandos, guías (incluidas la de lenguajes y extensiones, armada con `TiposArchivo` del motor, y la de scripts del motor, armada con `FileManager::plantillaScript`) y documentos del usuario; validación de scripts | `BaseConocimiento::global()`, `contextoParaPedido()`, `documentoScripts()`, `problemaDeScript()` | Nucleo/AyudaComandos, motor |
| `Agente/Subagentes` | Instrucciones de Silvy y subagentes; delegación de tareas | `GestorSubagentes`, `LanzarSubagentesTool`, `instruccionesPrincipal()` | Agente/LLM, Conocimiento |
| `Agente/Sesiones` | Conversaciones independientes persistidas | `SesionChat`, `AlmacenSesiones` | Agente (SesionAgente) |
| `Agente/Tareas` | Tarea del agente: objetivo y cierre que escribe el modelo, formato del contexto, respaldo | `TareaAgente`, `bloqueTareaAnterior()`, `pedidoDeObjetivo()`, `leerCierre()`, `consultar()` | Agente/LLM |
| `Agente/LLM/Config` | Configuración persistida y tipos del chat | `LlmConfig`, `ChatMessage`, `AgentEvent` | — |
| `Agente/LLM/Cliente` | Cliente HTTP OpenAI-compatible con streaming SSE; descargas; túneles (slots) por sesión hacia un servidor local | `LlmGateway`, `LlmGateway::setTunel()`, `AsignadorTuneles`, `http::descargar()`, `http::post()` | libcurl |
| `Agente/LLM/Bucle` | Bucle modelo → herramientas → modelo; aclaraciones del usuario durante el turno | `AgentLoop`, `ToolRegistry`, `PromptToolParser`, `BuzonInstrucciones`, `FormatoAclaracion` | Cliente, Config |
| `Agente/LLM/Herramientas` | Comandos del motor como tools, `buscar_comandos`, `ejecutar_comando` y perfiles | `EngineTool`, `crearRegistroPerfil(harness, perfil)`, `comandoPermitido()` | Bucle, Harness, Conocimiento |
| `Agente/LLM/HuggingFace` | Búsqueda, metadatos, variantes y descarga verificada | `HfApi` | Cliente |
| `Agente/LLM/Local` | Binario llama-server, modelos locales, ciclo de vida del servidor | `LocalInferenceManager`, `InstaladorLlama`, `RegistroServidor` | Cliente, HuggingFace |
| `Tui/` | Interfaz FTXUI: secciones, chat, modelos, ayuda | `ejecutarTui()`, `AplicacionTui` | Nucleo, Agente |
| `Tui/Configuracion`, `Tui/Terminal/PreferenciasTui` | Sección Configuración: rutas (`cli.config.*`), idioma y paleta editable en vivo, persistidos en `<config>/tui.json` | `tui::coloresTema()`, `tui::fijarColor()`, `tui::cargarPreferencias()` | Agente/LLM/Config |
| `Agente/Documentos` | RAG de documentos del usuario: extracción de texto, fragmentos, base de vectores FVDB, embeddings (cliente y servidor local Qwen3), búsqueda | `BaseDocumentos`, `AlmacenVectores`, `ServidorEmbeddings`, `extraerTexto()`, `BuscarDocumentosTool` | Agente/LLM, Conocimiento, miniz |
| `Tui/Chat` | Modelo del chat (sesiones, mensajes, subagentes), monitor de eventos y su dibujo; mensajes durante el razonamiento (agregar o encolar) y edición o eliminación de mensajes propios | `chat::SesionTui`, `chat::MonitorSesion`, `chat::MonitorTranscripcion` | Agente (eventos, sesiones) |
| `Util/` | Portapapeles (herramienta del sistema u OSC 52), métricas | `util::Clipboard::copiar()`, `secuenciaOsc52()`, `metricas::Cronometro` | — |

## 4. Flujos principales

Los diagramas de secuencia de cada flujo están en
[uml/ArquitecturaUML.es.md](uml/ArquitecturaUML.es.md#3-diagramas-de-secuencia).

**Petición JSON.** `main.cpp` lee la `Solicitud` → `ejecutarSolicitud()`
traduce el texto a `ComandoCLI` → `switch` exhaustivo → manejador de
`Comandos/` → valida, resuelve el proyecto, opera sobre el motor →
`ResultadoComando` → `main.cpp` imprime JSON y sale con su código.

**Chat con Silvy.** La TUI lanza un hilo por orden con la sesión en la que
se escribió → `Silvy::procesar(texto, sesion.agente, onEvent)`:
- `/nueva`, `/sesion N`… (sin punto): órdenes de sesión, las atiende la TUI;
- texto con `/` al inicio: comando CLI directo vía `Harness`;
- resto: Silvy arma un pedido nuevo:
  1. un `AgentLoop` con **su propio gateway** (por eso varias sesiones pueden
     correr a la vez) y las herramientas del perfil `completo`, más
     `buscar_comandos`, `ejecutar_comando` y `lanzar_subagentes`;
  2. las instrucciones de `instruccionesPrincipal()` (cómo trabajar, cuándo
     delegar, proyecto activo);
  3. la **documentación recuperada** por `BaseConocimiento` para el pedido,
     que se antepone al mensaje del usuario solo durante el turno;
  4. el bucle envía el historial al modelo en streaming; cada tool call se
     sanea (`ToolCallSanitizer`), se ejecuta (→ `Harness` → núcleo) y su
     resultado vuelve al modelo, hasta 10 rondas (se corta si una misma
     herramienta falla 3 veces). Los eventos actualizan el chat en vivo.

**Subagentes.** `lanzar_subagentes` (como mucho 2 tandas por pedido) valida las tareas y las pasa a
`GestorSubagentes`, que por cada una crea un bucle nuevo (gateway propio,
historial vacío, herramientas de su perfil, sin `lanzar_subagentes`, máximo 6
rondas) con `instruccionesSubagente()` y la documentación de su tarea. Corren
en tandas de `subagentesParalelos` (1 con servidor local). Cada subagente
tiene un id único: emite `SubagenteInicio` (con la tarea completa), todos sus
eventos con ese id en `origen`, y `SubagenteFin` (con el informe). Sus
informes (recortados a 1500 caracteres) vuelven al principal como resultado
de la herramienta.

**Monitoreo en el chat.** `AgentLoop` emite, además del texto y las
herramientas, `Pensamiento` (razonamiento del proveedor —`reasoning_content`,
`reasoning`— o entre `<think>`, separado por `SeparadorPensamiento`; no entra
al historial) y `Estado` (documentación recuperada, cada ronda con tiempo y
tokens —`LlmGateway::ultimaLlamada()`—, cortes y errores; una nota con
`callId` reemplaza a la anterior con el mismo id). En la TUI,
`chat::MonitorSesion` (`Tui/Chat/MonitorChat.h`) convierte los eventos en
mensajes en el orden en que ocurren: lo que el modelo escribió antes de una
llamada pasa a «Piensa», cada herramienta es un mensaje con su resumen y su
detalle completo (argumentos y resultado, que se ve con un clic), el
contenido que una llamada escribe en un archivo (`content`) va en un mensaje
`Codigo` debajo de ella, con nombre, lenguaje (`conocimiento::lenguajePorExtension`),
tipo en el motor (`TiposArchivo`) y el `resumen` que el modelo agrega a la
llamada (las herramientas lo quitan antes de ejecutar el comando), y los
eventos de cada subagente van a su propia transcripción
(`chat::SubagenteTui`) mientras la sesión muestra una línea de resumen. Un
clic en ese resumen (o `/sub N`) cambia el panel a la vista del subagente;
`Esc` vuelve. El desplazamiento se mide desde el final y el panel lo
compensa con las líneas nuevas, así lo que llega no mueve la vista de quien
está leyendo más arriba. En modo prompt los resultados vuelven al modelo
marcados como salida de sus propias herramientas (`resultadosParaModelo()`),
no como texto del usuario. Todo se persiste con la sesión (`SubagenteSesion`). La lógica
del monitor no depende de FTXUI y se prueba en `agente-tests`.

Si la plantilla del modelo no admite tools nativas (`/props` de
llama-server), el bucle usa el **modo prompt**: las herramientas se describen
en el prompt de sistema y `PromptToolParser` extrae bloques ```` ```tool_call ````
del texto: también llamadas sin cerco o en ```` ```json ````, varias en un mismo
bloque (una por línea, aunque a alguna le falten llaves) y la forma
`{"comando": …, "argumentos": …}` sin `"tool"`, que se toma como
`ejecutar_comando`; el código entre comillas triples (`"""…"""`) o con saltos
de línea crudos dentro de una cadena se repara; un nombre de herramienta mal
escrito se corrige solo si está cerca, y en un comando solo dentro de su grupo y
por un carácter de la última parte (`scene.object.rename` no pasa a ser
`scene.object.create`); una herramienta inexistente con nombre de respuesta («fin
de la respuesta») y un único texto se toma como la respuesta final
(`detalle::respuestaDisfrazada`). Los resultados vuelven al modelo entre las marcas
`[SALIDA DE TUS HERRAMIENTAS …]` y `[FIN DE LA SALIDA]`, que se quitan de la
respuesta final si el modelo las copia (`sinMarcasDeSalida`).

El `Harness` del agente corrige y protege sus llamadas
(`setCorreccionesDelAgente`): `"<proyecto>"` (el marcador de los ejemplos)
cuenta como proyecto ausente y un argumento en `null`, como ausente; `"true"` o
`"false"` como texto en un argumento booleano pasan a booleano; las rutas de `project.src.*` pierden un `src/`
inicial; `project.src.create` sobre un archivo existente falla con
`file_exists` salvo que la llamada traiga `"sobrescribir": true` o que el contenido nuevo conserve todas las líneas del
archivo (argumento del agente que no llega al comando), y un archivo con contenido sin extensión
falla con `missing_extension`.

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

El servidor de embeddings tiene su propio registro (`embeddings.pid`,
`kRegistroEmbeddings`): `LocalInferenceManager::setModoEmbeddings(true)` lo
lanza con `--embedding --pooling last` y su propio log, y los dos conviven.

**Documentos del usuario (RAG).** Al abrir la TUI un hilo de fondo prepara
la carpeta de documentos y, si tiene archivos, `ServidorEmbeddings::asegurar()`
descarga Qwen3-Embedding si falta y levanta su servidor;
`BaseDocumentos::actualizar()` compara mtime y tamaño con la base
(`AlmacenVectores`, formato propio FVDB), extrae el texto de lo nuevo
(`ExtractorTexto`: pdftotext o extractor propio, ZIP+XML con miniz, HTML),
lo parte en fragmentos con su página, los embebe por lotes y publica la base
nueva de una vez (las búsquedas no esperan). En cada pedido, Silvy agrega al
contexto del turno los fragmentos que superan el umbral de similitud, y el
agente tiene `buscar_documentos`. Sin embeddings, la búsqueda es léxica
(`IndiceBm25`).

**Tareas.** Con las tareas activas (`configurarLlm`; `configurarConFabrica`
las deja apagadas salvo `setTareasActivas(true)`), `Silvy::enviarAlLlm`
agrega al contexto del turno la tarea anterior (`bloqueTareaAnterior`: «##
Tarea anterior / Lo que hice / Resumen»), pide al modelo en una llamada corta
sin herramientas la línea «Ahora: …» con la documentación del turno y el
contexto del motor (`pedidoDeObjetivo` → `consultar` → `leerObjetivo`) y
corre el bucle con el encabezado «## Tarea actual» y el objetivo tras el
pedido (`AgentLoop::setEncabezadoPedido`, parámetro `trasPedido`). Al
terminar, otra llamada corta con las herramientas ejecutadas y la respuesta
pide «Lo que hice / Resumen» (`pedidoDeCierre` → `leerCierre`); si el modelo
no respeta el formato, el pedido se canceló o ninguna herramienta salió bien,
`cierreDeRespaldo` lo arma con las herramientas. La tarea queda en `SesionAgente::tareaAnterior` y se
persiste con la sesión. El objetivo en una llamada aparte evita que el modelo
escriba el «Ahora:» y termine el turno sin actuar.

**Información de los comandos para el modelo.** Los esquemas de las
herramientas del motor se generan de `AyudaComandos` (`specDesdeAyuda`):
la misma fuente que la base de conocimiento, con tipo y descripción de cada
argumento y `project` primero; la firma del modo prompt muestra los tipos
(`scene.object.create(project*:texto, name*:texto, parentId:entero)`). El
`Harness` de cada pedido completa `project` con el proyecto de trabajo (el
activo de la sesión, o el único que existe) y corrige al nombre exacto un
`project` que trae la ruta del proyecto o el nombre con otras mayúsculas
(`Harness::proyectoReconocido`). Un error de argumentos devuelve
el uso correcto del comando (`BaseConocimiento::usoDeComando`). Las guías se
muestran con cada paso como la llamada real (`guiaEjecutable`) y suman la
documentación de los comandos que nombran.

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
- **Un gateway por bucle**: cURL no es seguro entre hilos, así que cada pedido
  y cada subagente crea el suyo; `Silvy` solo guarda la configuración
  (`EstadoLlm`, reemplazable entera al cambiar de modelo).
- **Comandos serializados**: `Harness::ejecutar()` toma un mutex recursivo
  global: el motor y la auditoría no son seguros entre hilos, y los comandos
  tardan milisegundos (lo lento, el modelo, sí corre en paralelo).
- **Sesiones**: el hilo de cada orden guarda un `shared_ptr` a su sesión; una
  sesión ocupada no se borra, limpia ni guarda desde la UI (la guarda su hilo
  al terminar).
- **Estado compartido con hilos desacoplados**: `EstadoCompartido` (con la
  pantalla protegida por mutex) evita que un hilo toque la pantalla después de
  destruida; los estáticos que usan esos hilos se crean con `new` y no se
  destruyen nunca, para que sigan válidos durante la salida del proceso.
- **cURL** se inicializa una vez en `main` (`http::inicializarCurl()`) y todas
  las transferencias usan `CURLOPT_NOSIGNAL`.
- **Cancelación** de una respuesta en curso: `LlmGateway::setCancelFlag()`
  (el callback de progreso de cURL aborta la transferencia).
- **Túneles por sesión**: con un servidor local, `Silvy::enviarAlLlm` reserva
  con `AsignadorTuneles` el slot de la sesión (`SesionAgente::id`) para todo
  el pedido (objetivo, bucle, subagentes y cierre) y cada gateway lo manda
  como `id_slot`. Así una sesión no reaprovecha la caché KV de otra (con
  `--cache-reuse`, el servidor desplaza trozos calculados atendiendo a la
  conversación anterior). Las sesiones hacen cola: si no hay slot libre, cede el
  suyo la sesión que entró primero entre las no reservadas (FIFO); su caché se
  guarda (`POST /slots/<i>?action=save`, `<sesión>.bin` en `slots/`) y la que
  entra restaura la suya si la tiene (`action=restore`, el archivo se borra) o
  el slot se vacía (`action=erase`). Con todos reservados el pedido va sin
  túnel. Se conservan las `kMaxCachesGuardadas` (4) cachés más recientes. El
  servidor arranca con `-np 2 --kv-unified --slot-save-path <slots>
  --no-cache-idle-slots` (sin esta última, con KV unificado el servidor vacía
  los slots ociosos y no queda caché que guardar); cada opción se agrega solo
  si el binario la admite (`llama-server --help`).
- **Confirmación de borrados**: `ejecutar_comando` pide permiso
  (`ToolContext::requestPermission`) antes de `scene.object.delete`,
  `script.delete`, `asset.delete` y `project.src.delete`, con una descripción
  de lo que se borra. `AgentLoop::setSolicitudPermiso` lo conecta con
  `SesionAgente::confirmar`, que bloquea el hilo del pedido hasta la respuesta:
  la TUI muestra la pregunta en la sesión y espera en una variable de condición
  (con `chatMutex_`); cancelar el pedido cuenta como no. Sin `confirmar`, se
  niega. Subagentes incluidos.
- **Papelera**: `moverAPapelera` mueve el archivo o la carpeta a
  `<papelera>/<id>/` y anota en `indice.json` su ruta, tipo, fecha y las
  referencias quitadas; `anotarObjetoEnPapelera` anota el prefab de un objeto.
  `trash.restore` mueve el archivo de vuelta y vuelve a agregar los
  componentes con `scene.object.addcomponent` (Script, Model, AudioSource) o
  restaura el mapa del Material; para un objeto, instancia el prefab y quita
  su borrado del historial de deshacer. `scene.undo` de un borrado saca la
  entrada de la papelera (`quitarDePapeleraPorPrefab`) y `scene.redo` la
  vuelve a anotar.
- **Borrar un objeto con hijos**: `SceneRegistry::deleteObject` del motor solo
  saca el nodo del árbol; los descendientes seguirían en el registro y
  reaparecerían en la raíz al guardar. `borrarConDescendientes` los borra de las
  hojas hacia arriba. La copia para `scene.undo` es un prefab del subárbol
  (`Prefab::guardarDesdeObjeto`); como el constructor de `Prefab` crea
  `<raíz de assets>/Prefabs`, la raíz se fija temporalmente en la papelera.
- **Aclaraciones durante el turno**: la TUI deja el mensaje en el
  `BuzonInstrucciones` de la sesión (seguro entre hilos); `AgentLoop` las toma
  al empezar cada ronda y las incorpora según `FormatoAclaracion` (por defecto
  `MensajeNuevo`, un mensaje propio del usuario: el que mejor atendió el modelo
  en las pruebas) junto con la documentación del RAG para su texto
  (`setDocumentacionAclaraciones`), que se recorta al terminar el turno; si iba
  a terminar, da una ronda más. Las que llegan cuando el bucle ya cerró van como
  el pedido siguiente, antes de la cola.
- **Edición o eliminación de mensajes**: el hilo de la UI recorta los mensajes
  del chat y sube `SesionTui::generacion`; el hilo del pedido cancelado
  compara la generación antes de cada escritura y deja de escribir. El
  historial del modelo lo usa ese hilo, así que el recorte
  (`SesionAgente::descartarPedidos`, que también vuelve a la tarea
  correspondiente) se aplica cuando termina (`pedidosADescartar`).

## 6. Datos en disco

| Archivo | Ubicación por defecto | Escribe |
|---|---|---|
| `llm-config.json` | `$FUNSHI_CONFIG_DIR`, o `.agentes/` del cwd o de sus padres, o global (`~/.agentes`, `%APPDATA%\FunshiEngine`) | `LlmConfig::save()` (atómico) |
| `metricas.jsonl` | junto a `llm-config.json` | `metricas::registrar()` (con `FUNSHI_PERF=1`) |
| `silvy-auditoria.log` | `.agentes/` | `Auditoria` |
| `sesiones/<id>.json` | junto a `llm-config.json` | `AlmacenSesiones` (atómico; historial recortado a 200 mensajes) |
| `conocimiento/*.md` | junto a `llm-config.json` | el usuario (lo lee `BaseConocimiento`) |
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

**Enseñarle algo al agente**
- Un comando nuevo queda documentado solo con su entrada en `Nucleo/AyudaComandos.cpp`.
- Una receta de varios pasos: una `Guia` en `Agente/Conocimiento/GuiasAgente.cpp`.
- Conocimiento del proyecto del usuario: `.md` en `<config>/conocimiento/`.
- Si una consulta en español no recupera lo correcto, agrega el sinónimo en
  `IndiceBm25.cpp` y el caso a `tests/agente/PruebasConocimiento.cpp`.

**Agregar un perfil de subagente**: entrada en `perfilesHerramientas()` y su
conjunto de comandos en `PerfilesHerramientas.cpp`; el enum del esquema de
`lanzar_subagentes` lo lista.

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
