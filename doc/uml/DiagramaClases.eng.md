# Class Diagram - FunshiEngineCLI

Main CLI classes and structs, grouped by module. Each diagram shows the
relevant public API (not every private member). Overall architecture in
[ArquitecturaUML.eng.md](ArquitecturaUML.eng.md).

Conventions:
- **Language**: English; the primary (Spanish) version is [DiagramaClases.es.md](DiagramaClases.es.md)
- **Format**: Mermaid `classDiagram`
- **Relations**: `<|--` inheritance, `*--` composition, `o--` aggregation, `-->` association, `..>` dependency
- A module's free functions are grouped in a class with the `<<funciones>>` (functions) stereotype

## 1. Core: request, response and dispatch

```mermaid
classDiagram
    class Solicitud {
        +string comando
        +json argumentos
        +optional~json~ id
        +string rutaProyecto
    }
    class Respuesta {
        +bool correcta
        +json resultado
        +string codigoError
        +string mensajeError
        +optional~json~ id
        +ok(json datos)$ Respuesta
        +fallo(string codigo, string mensaje)$ Respuesta
        +conId(json id) Respuesta
        +aJson() json
        +aTexto() string
    }
    class ResultadoComando {
        +Respuesta respuesta
        +int codigoSalida
        +exito(json datos)$ ResultadoComando
        +fallo(string codigo, string mensaje, int codigoSalida)$ ResultadoComando
    }
    class ComandoCLI {
        <<enumeration>>
        Ayuda
        Version
        ProyectoCrear
        EscenaObjetoCrear
        AgenteProcesar
        ...48 commands
    }
    class NucleoCLI {
        <<funciones>>
        +ejecutarSolicitud(Solicitud) ResultadoComando
        +textoAyuda() string
    }
    class CatalogoComandos {
        <<funciones>>
        +comandoDesdeTexto(string) ComandoCLI
        +textoDesdeComando(ComandoCLI) string
        +todosLosComandos() vector~ComandoCLI~
    }
    class Comandos {
        <<funciones>>
        +escenaObjetoCrear(json) ResultadoComando
        +proyectoCrear(json) ResultadoComando
        +scriptCompilar(json) ResultadoComando
        +... one handler per command
    }

    ResultadoComando *-- Respuesta
    NucleoCLI ..> Solicitud : receives
    NucleoCLI ..> ResultadoComando : returns
    NucleoCLI ..> CatalogoComandos : translates
    NucleoCLI ..> ComandoCLI : exhaustive switch
    NucleoCLI ..> Comandos : delegates
```

## 2. Domain: scene, validation and projects

```mermaid
classDiagram
    class ContextoEscena {
        <<RAII>>
        +ContextoEscena(string nombreProyecto)
        +registro() SceneRegistry
        +editor() EditorController
        +serializer() SceneSerializer
    }
    class SerializacionEscena {
        <<funciones>>
        +componenteJson(Component*) json
        +gameObjectJson(GameObject*) json
        +sceneHierarchyJson(SceneRegistry*) json
        +findObjectById(SceneRegistry*, int) GameObject*
        +findObjectByTag(SceneRegistry*, string) GameObject*
    }
    class ValidacionArgs {
        <<funciones>>
        +validarArgumentos(json, tabla RequisitoArg, string error) bool
        +argumentoProyecto(json, string nombre, string error) bool
        +dimensionDesdeArgs(json, int dimension, string error) bool
    }
    class ResolucionProyectos {
        <<funciones>>
        +resolverProyecto(string ruta, Proyecto, string error) bool
        +resolverProyectoPorNombre(string nombre, Proyecto, string error) bool
    }
    class ComandosComun {
        <<funciones>>
        +resolverProyectoDesdeArgs(json, string nombre, ResultadoComando) bool
        +rutaSegura(path raiz, path destino, string mensaje, ResultadoComando) bool
    }
    class ComandosEscenaInterno {
        <<funciones>>
        +guardarEscena(ContextoEscena, string proyecto)
        +registrarOperacionUndo(string proyecto, string tipo, int id, json antes, json despues)
        +tipoComponenteNombre(Component*) string
    }
    class SceneRegistry {
        <<motor>>
    }
    class SceneSerializer {
        <<motor>>
    }

    ContextoEscena *-- SceneRegistry
    ContextoEscena *-- SceneSerializer
    ComandosEscenaInterno ..> ContextoEscena
    ComandosEscenaInterno ..> ComandosComun
    ComandosComun ..> ValidacionArgs
    ComandosComun ..> ResolucionProyectos
    SerializacionEscena ..> SceneRegistry
```

## 3. Agent: Silvy, Harness and Auditing

```mermaid
classDiagram
    class Silvy {
        -Harness harness_
        -shared_ptr~EstadoLlm~ llm_
        -SesionAgente sesionPorDefecto_
        +configurarLlm(LlmConfig, bool nativeToolCalls) bool
        +configurarConFabrica(FabricaGateway, bool, int)
        +setTareasActivas(bool)
        +tieneLlm() bool
        +procesar(string texto, onEvent) RespuestaProcesada
        +procesar(string texto, SesionAgente, onEvent) RespuestaProcesada
        +modeloActivo() string
        -ejecutarComandoCli(string) RespuestaProcesada
        -enviarAlLlm(string, SesionAgente, onEvent) RespuestaProcesada
    }
    class SesionAgente {
        +string id
        +vector~ChatMessage~ historial
        +string proyectoActivo
        +TareaAgente tareaAnterior
        +vector~TareaAgente~ tareasPrevias
        +shared_ptr~atomic bool~ cancelar
        +shared_ptr~BuzonInstrucciones~ aclaraciones
        +descartarPedidos(size_t)
    }
    class BuzonInstrucciones {
        +agregar(string)
        +tomar() vector~string~
        +vacio() bool
    }
    class AsignadorTuneles {
        +paraServidor(string baseUrl)$ shared_ptr~AsignadorTuneles~
        +reservar(string sesion) Reserva
        +total() int
    }
    class TareaAgente {
        +string pedido
        +string objetivo
        +vector~string~ hecho
        +string resumen
        +bool resumenDelModelo
        +aJson() json
        +desdeJson(json)$ TareaAgente
    }
    class EstadoLlm {
        +FabricaGateway fabrica
        +bool nativas
        +int subagentesParalelos
        +string modelo
        +bool tareas
    }
    class RespuestaProcesada {
        +string texto
        +ResultadoComando resultado
        +bool fueComandoCli
        +bool error
    }
    class Harness {
        -Auditoria auditoria_
        +ejecutar(Solicitud, bool modificaDisco) ResultadoComando
        +ejecutarTexto(string comando, json args, bool modificaDisco) ResultadoComando
        +auditoria() Auditoria
    }
    class Auditoria {
        +Auditoria(string rutaLog)
        +registrar(EntradaAuditoria)
        +entradas() vector~EntradaAuditoria~
        +cambiosEnDisco() vector~EntradaAuditoria~
        +resumenReciente(size_t n) string
    }
    class EntradaAuditoria {
        +string marcaTiempo
        +string comando
        +json argumentos
        +bool ok
        +string codigoError
        +int codigoSalida
        +bool modificaDisco
    }

    Silvy --> Harness
    Silvy *-- EstadoLlm
    Silvy ..> SesionAgente : processes
    SesionAgente *-- TareaAgente : previous task
    SesionAgente *-- BuzonInstrucciones : inbox
    AgentLoop ..> BuzonInstrucciones
    Silvy ..> AsignadorTuneles : session tunnels
    Silvy ..> AgentLoop : creates one per request
    Silvy ..> RespuestaProcesada
    Harness --> Auditoria
    Auditoria *-- EntradaAuditoria
    Harness ..> NucleoCLI : ejecutarSolicitud()
```

## 4. Agente/LLM: loop, tools and client

```mermaid
classDiagram
    class AgentLoop {
        -shared_ptr~LlmGateway~ gateway_
        -shared_ptr~ToolRegistry~ registry_
        -bool nativeToolCalls_
        +int maxIterations = 8
        +run(mensajes, onEvent, contextoTurno, trasPedido) AgentAnswer
        +setInstrucciones(string)
        +setEncabezadoPedido(string)
        +setBuzon(shared_ptr~BuzonInstrucciones~)
        +setFormatoAclaracion(FormatoAclaracion)
        +setResolutorAlias(ResolutorAlias)
        +setCancelFlag(shared_ptr~atomic bool~)
        +setNativeToolCalls(bool)
        -buildSystemPrompt() string
        -executeTool(string nombre, json args, string callId) ToolResult
    }
    class ToolCallSanitizer {
        <<utilidad>>
        +args(string esperado, string argsJson)$ SanitizedToolCall
        +block(string crudo)$ SanitizedToolCall
        -repairJson(string)$ string
        -validateShape(json, string*)$ SanitizedToolCall
    }
    class PromptToolParser {
        +feed(string delta) string
        +flush() string
        +drain() vector~string~
        +llamadasSinCerco(string)$ vector~string~
    }
    class ToolRegistry {
        -vector~shared_ptr~Tool~~ tools_
        +add(shared_ptr~Tool~)
        +byName(string) shared_ptr~Tool~
        +specs() vector~ToolSpec~
        +describe() string
    }
    class Tool {
        <<abstract>>
        +spec()* ToolSpec
        +category() ToolCategory
        +execute(json args, ToolContext)* ToolResult
    }
    class EngineTool {
        <<abstract>>
        #shared_ptr~Harness~ harness_
        #executeCommand(string comando, json args) ToolResult
    }
    class SceneObjectCreateTool
    class ProjectListTool
    class ScriptCompileTool
    class BuscarComandosTool {
        +execute(consulta, max) ToolResult
    }
    class EjecutarComandoTool {
        +bloqueados()$ set~string~
        +execute(comando, argumentos) ToolResult
    }
    class LlmGateway {
        <<interface>>
        +chatStream(mensajes, tools, onChunk)* bool
        +chatText(mensajes)* string
        +chat(mensajes)* string
        +listModels()* vector~string~
        +close()*
        +setTunel(int slot)
    }
    class OpenAiCompatibleGateway {
        -CURL* curl_
        -shared_ptr~atomic bool~ cancel_
        +setCancelFlag(shared_ptr~atomic bool~)
    }
    class LlmException {
        +Code code
        +fromHttp(int status, string body, bool local)$ LlmException
    }
    class ChatMessage {
        +string role
        +string content
        +optional~json~ toolCalls
        +optional~string~ toolCallId
    }
    class ToolSpec {
        +string name
        +string description
        +string parametersSchema
    }
    class AgentEvent {
        +Type type
        +string delta
        +string callId
        +string name
        +string argsJson
        +bool ok
        +string result
        +string origen
        +estado(texto, id)$ AgentEvent
    }
    class TipoEvento {
        <<enumeration>>
        Text
        ToolStart
        ToolEnd
        Pensamiento
        Estado
        SubagenteInicio
        SubagenteFin
    }
    class SeparadorPensamiento {
        -bool dentro_
        -string pendiente_
        +agregar(fragmento) Partes
        +terminar() Partes
    }
    class EstadisticasLlamada {
        +long msTotal
        +long msPrimerToken
        +long tokensEntrada
        +long tokensSalida
    }
    class AgentAnswer {
        +string text
        +vector~ExecutedTool~ toolCalls
    }

    AgentLoop --> LlmGateway
    AgentLoop --> ToolRegistry
    AgentLoop ..> ToolCallSanitizer
    AgentLoop ..> PromptToolParser : prompt mode
    AgentLoop ..> AgentEvent : emits
    AgentEvent --> TipoEvento
    AgentLoop ..> SeparadorPensamiento : per round
    LlmGateway ..> EstadisticasLlamada : ultimaLlamada()
    AgentLoop ..> AgentAnswer : returns
    ToolRegistry o-- Tool
    Tool <|-- EngineTool
    EngineTool <|-- SceneObjectCreateTool
    EngineTool <|-- ProjectListTool
    EngineTool <|-- ScriptCompileTool
    EngineTool <|-- EjecutarComandoTool
    Tool <|-- BuscarComandosTool
    EngineTool --> Harness
    LlmGateway <|.. OpenAiCompatibleGateway
    OpenAiCompatibleGateway ..> LlmException : throws
    LlmGateway ..> ChatMessage
    Tool ..> ToolSpec
```

> There are 23 `EngineTool` tools (project, scene, files, scripts, assets,
> diagnostics); the diagram shows three. All of them are created by
> `createEngineToolRegistry(harness)`.

## 5. Agente/LLM: local inference and Hugging Face

```mermaid
classDiagram
    class LocalInferenceManager {
        -path baseDir_
        -path modelsDir_
        -path llamaServerBinary_
        -LocalServerInfo serverInfo_
        -LocalInferenceState state_
        +ensureLlamaServer(progreso) bool
        +downloadModel(ModelVariant, progreso) bool
        +startServer(path modelo, int puerto, int gpuLayers, int contexto, progreso) bool
        +stopServer()
        +detachServer()
        +consultarCapacidades() CapacidadesServidor
        +getServerInfo() LocalServerInfo
    }
    class LocalInferenceState {
        <<enumeration>>
        IDLE
        PREPARING_SERVER
        DOWNLOADING_MODEL
        STARTING_SERVER
        RUNNING
        STOPPING
        ERROR
    }
    class LocalServerInfo {
        +string baseUrl
        +int port
        +string modelPath
        +pid_t pid
        +bool isRunning
    }
    class CapacidadesServidor {
        +bool consultado
        +bool herramientasNativas
        +int contexto
    }
    class InstaladorLlama {
        -path baseDir_
        +instalada() InstalacionLlama
        +asegurarActualizado(progreso) EstadoLlamaServer
        +directorioBinarios() path
    }
    class detalle_llama {
        <<funciones>>
        +buscarUltimaRelease(PlataformaLlama, ReleaseLlama) ResultadoPaso
        +instalarRelease(path dirBin, ReleaseLlama, PlataformaLlama, string accion, informar, path servidor) ResultadoPaso
    }
    class EstadoLlamaServer {
        +bool ok
        +bool actualizado
        +bool sinConexion
        +string tag
        +path servidor
        +string mensaje
    }
    class RegistroServidor {
        <<funciones>>
        +registrarServidor(path baseDir, int pid, int puerto)
        +detenerServidorRegistrado(path baseDir)
        +limpiarServidoresHuerfanos(path baseDir, path dirBinarios)
        +instalarCierreServidorPorSenal()
    }
    class ArchivoModeloGguf {
        <<funciones>>
        +inspeccionarArchivoModelo(path) EstadoArchivoModelo
    }
    class HfApi {
        -unique_ptr~Impl~ pImpl_
        -string cacheDir_
        +search(SearchFilters, progreso) SearchResult
        +getModelInfo(string id, progreso, bool forzar) optional~ModelInfo~
        +getVariants(string id, progreso, bool forzar) vector~ModelVariant~
        +downloadVariant(ModelVariant, string destino, progreso) bool
        +lastErrorWasOffline() bool
        +cleanCache()
    }
    class ModelInfo {
        +string id
        +int64 downloads
        +bool gated
        +vector~ModelVariant~ variants
    }
    class ModelVariant {
        +string filename
        +string quantization
        +uint64 sizeBytes
        +string downloadUrl
        +string sha256
    }
    class HttpDescarga {
        <<funciones>>
        +get(url, cuerpo, cabeceras, userAgent, timeoutMs) ResultadoHttp
        +descargar(url, destino, OpcionesDescarga, progreso) ResultadoHttp
    }
    class LlmConfig {
        +LlmMode mode
        +string remoteBaseUrl
        +string modelName
        +int contextSize
        +int gpuLayers
        +string modelsDir
        +string localInferenceDir
        +load()$ LlmConfig
        +save()
        +configPath()$ path
        +modelsDirEfectivo() path
        +localInferenceDirEfectivo() path
    }

    LocalInferenceManager *-- LocalServerInfo
    LocalInferenceManager --> LocalInferenceState
    LocalInferenceManager ..> CapacidadesServidor
    LocalInferenceManager ..> InstaladorLlama : ensureLlamaServer
    LocalInferenceManager ..> HfApi : downloadModel
    LocalInferenceManager ..> RegistroServidor : start / stop
    InstaladorLlama ..> detalle_llama
    InstaladorLlama ..> EstadoLlamaServer
    detalle_llama ..> HttpDescarga
    HfApi ..> HttpDescarga
    HfApi ..> ModelInfo
    ModelInfo *-- ModelVariant
```

## 6. Knowledge, subagents and sessions

```mermaid
classDiagram
    class IndiceBm25 {
        +agregar(string texto) size_t
        +construir()
        +buscar(string consulta, size_t k) vector
        +tokenizar(string)$ vector~string~
        +expandirConsulta(string)$ vector~string~
    }
    class BaseConocimiento {
        -vector~Documento~ documentos_
        -IndiceBm25 indice_
        +global()$ shared_ptr~BaseConocimiento~
        +buscar(string consulta, size_t k) vector~ResultadoBusqueda~
        +comando(string nombre) Documento*
        +contextoParaPedido(string pedido, set herramientas, size_t k) string
        +formatear(Documento, set herramientas)$ string
    }
    class Documento {
        +Tipo tipo
        +string id
        +string titulo
        +string contenido
    }
    class PerfilesHerramientas {
        <<funciones>>
        +perfilesHerramientas() vector
        +comandoPermitido(string perfil, string comando) bool
        +crearRegistroPerfil(Harness, string perfil) ToolRegistry
    }
    class GestorSubagentes {
        -FabricaBucles fabrica_
        -int maxParalelo_
        -string proyectoActivo_
        +ejecutar(vector~TareaSubagente~, onEvent) vector~InformeSubagente~
        +resumen(vector~InformeSubagente~)$ string
    }
    class LanzarSubagentesTool {
        +leerTareas(json, vector, string error)$ bool
        +execute(tareas) ToolResult
    }
    class TareaSubagente {
        +string titulo
        +string instrucciones
        +string contexto
        +string perfil
    }
    class InformeSubagente {
        +string id
        +string titulo
        +bool ok
        +string informe
        +int herramientasUsadas
    }
    class Lenguajes {
        <<funciones>>
        +lenguajes() vector~Lenguaje~
        +lenguajePorExtension(string) Lenguaje
        +tipoEnMotor(string ruta) string
        +extensionDeProyecto() string
        +documentoLenguajes() string
    }
    class PlantillasAgente {
        <<funciones>>
        +instruccionesPrincipal(bool conSubagentes, string proyecto) string
        +instruccionesSubagente(TareaSubagente, string proyecto) string
    }
    class SesionChat {
        +string id
        +string titulo
        +int64 creada
        +int64 actualizada
        +vector~MensajeSesion~ mensajes
        +vector~SubagenteSesion~ subagentes
        +SesionAgente agente
        +nueva(string titulo)$ SesionChat
        +tituloDesde(string pedido)$ string
    }
    class SubagenteSesion {
        +string id
        +string titulo
        +string perfil
        +string instrucciones
        +string contexto
        +string estado
        +vector~MensajeSesion~ mensajes
    }
    class MensajeSesion {
        +string rol
        +string texto
        +string detalle
        +string ref
    }
    class AlmacenSesiones {
        -path dir_
        +cargarTodas() vector~SesionChat~
        +guardar(SesionChat) bool
        +borrar(string id) bool
    }

    BaseConocimiento *-- IndiceBm25
    BaseConocimiento *-- Documento
    BuscarComandosTool ..> BaseConocimiento
    LanzarSubagentesTool --> GestorSubagentes
    LanzarSubagentesTool ..> TareaSubagente
    GestorSubagentes ..> InformeSubagente
    BaseConocimiento ..> Lenguajes : guia.lenguajes
    GestorSubagentes ..> PlantillasAgente
    GestorSubagentes ..> BaseConocimiento : documentation per task
    GestorSubagentes ..> PerfilesHerramientas : via FabricaBucles
    AlmacenSesiones ..> SesionChat
    SesionChat *-- MensajeSesion
    SesionChat *-- SubagenteSesion
    SubagenteSesion *-- MensajeSesion
    GestorSubagentes ..> AgentEvent : SubagenteInicio / SubagenteFin, origen = id
```

## 7. TUI

```mermaid
classDiagram
    class ComponentBase {
        <<FTXUI>>
        +Render() Element
        +OnEvent(Event) bool
    }
    class AplicacionTui {
        -ScreenInteractive* pantalla_
        -Silvy silvy_
        -vector~EstadoSeccion~ estados_
        -vector~shared_ptr~SesionTui~~ sesiones_
        -ModeloHFEstado modelosHFEstado_
        -shared_ptr~EstadoCompartido~ compartido_
        +OnRender() Element
        +OnEvent(Event) bool
        -recargarSeccionActual(bool informar)
        -eventoEnChat(Event) bool
        -eventoEnModelosHF(Event) bool
        -procesarOrdenChat(string)
        -iniciarAutoConfiguracionModelo(ModelVariant)
        -iniciarConfiguracionModeloLocal(path)
        -panelChat() Element
        -panelModelosHF() Element
        -panelAyuda() Element
        -aplicarSeleccion(Screen pantalla)
        -lanzarCopia(string texto)
    }
    class Clipboard {
        <<utilidad>>
        +copiar(string texto)$ ResultadoCopia
        +hayHerramientaSistema()$ bool
        +secuenciaOsc52(string texto)$ string
    }
    class ResultadoCopia {
        +bool ok
        +string metodo
        +string error
    }
    class SeccionTui {
        <<enumeration>>
        Proyectos
        Escena
        Scripts
        Diagnosticos
        Chat
        ModelosHF
        Configuracion
    }
    class ModeloHFEstado {
        <<enumeration>>
        LISTA_LOCALES
        LISTA_MODELOS
        LISTA_VARIANTES
        CONFIGURANDO
        ERROR_CONFIG
    }
    class MensajeChat {
        +Rol rol
        +string texto
        +string detalle
        +string ref
        +bool enCurso
        +bool ok
        +bool expandido
        +int version
        +vector~LineaChat~ lineasCache
    }
    class RolMensaje {
        <<enumeration>>
        Usuario
        Silvy
        Pensamiento
        Herramienta
        Subagente
        Tarea
        Codigo
        Estado
        Error
    }
    class SesionTui {
        +SesionChat datos
        +vector~MensajeChat~ mensajes
        +vector~shared_ptr~SubagenteTui~~ subagentes
        +string vista
        +bool ocupado
        +int desplazamiento
        +int lineasDibujadas
        +string enEspera
        +deque~string~ cola
        +int menuMensaje
        +unsigned generacion
        +subagente(id) SubagenteTui
        +cargarDesdeDatos()
        +volcarEnDatos()
    }
    class SubagenteTui {
        +SubagenteSesion datos
        +vector~MensajeChat~ mensajes
        +int numero
        +int acciones
        +string ultimaAccion
        +bool enCurso
    }
    class MonitorSesion {
        -MonitorTranscripcion principal_
        -map~string,MonitorTranscripcion~ subagentes_
        +esperar()
        +aplicar(AgentEvent)
        +cerrar(textoFinal, error)
    }
    class PreferenciasTui {
        <<funciones>>
        +coloresTema() vector~ColorTema~
        +fijarColor(clave, valor, error) bool
        +fijarIdioma(codigo) bool
        +cargarPreferencias(archivo)
        +guardarPreferencias(archivo) bool
    }
    class ColorTema {
        +string clave
        +string nombre
        +Color* destino
        +Rgb porDefecto
        +Rgb actual
    }
    class MonitorTranscripcion {
        -vector~MensajeChat~* mensajes_
        -long respuesta_
        -long pensamiento_
        -map~string,long~ herramientas_
        +esperar()
        +aplicar(AgentEvent)
        +cerrar(textoFinal, error)
    }
    class EstadoSeccion {
        +json items
        +string error
        +int seleccion
    }
    class EstadoCompartido {
        +mutex mutex
        +ScreenInteractive* pantalla
    }
    class EntradaRespaldo {
        +iniciar(ScreenInteractive*)
    }
    class AyudaComandos {
        <<funciones>>
        +ayudaComandos() vector~AyudaComando~
        +buscarAyudaComando(string) AyudaComando*
    }
    class InterfazTui {
        <<funciones>>
        +ejecutarTui(int seccionInicial) int
    }

    ComponentBase <|-- AplicacionTui
    AplicacionTui --> Silvy
    AplicacionTui --> SeccionTui
    AplicacionTui --> ModeloHFEstado
    AplicacionTui *-- SesionTui : sesiones_
    SesionTui *-- MensajeChat
    SesionTui *-- SubagenteTui
    SubagenteTui *-- MensajeChat
    MensajeChat --> RolMensaje
    AplicacionTui ..> MonitorSesion : one monitor per request
    MonitorSesion *-- MonitorTranscripcion
    MonitorSesion --> SesionTui
    MonitorTranscripcion --> MensajeChat
    AplicacionTui ..> PreferenciasTui : sección Configuración
    PreferenciasTui *-- ColorTema
    MonitorTranscripcion ..> Lenguajes : language and engine type
    AplicacionTui --> AlmacenSesiones
    AplicacionTui *-- EstadoSeccion
    AplicacionTui o-- EstadoCompartido : shared with threads
    AplicacionTui ..> AyudaComandos
    AplicacionTui ..> LocalInferenceManager
    AplicacionTui ..> HfApi
    InterfazTui ..> AplicacionTui : creates
    InterfazTui ..> EntradaRespaldo
    InterfazTui ..> RegistroServidor : on exit
    AplicacionTui ..> Clipboard : copies the selection
    Clipboard ..> ResultadoCopia
```


## 8. User documents and color picker

```mermaid
classDiagram
    class BaseDocumentos {
        -path carpeta_
        -AlmacenVectores almacen_
        -IndiceBm25 lexico_
        +global()$ BaseDocumentos
        +prepararCarpeta()
        +actualizar(ProveedorEmbeddings, progreso) bool
        +buscar(consulta, k, ProveedorEmbeddings) vector~ResultadoDocumento~
        +contextoParaPedido(pedido, ProveedorEmbeddings) string
        +fragmentar(texto, maximo, solape)$ vector~Fragmento~
    }
    class AlmacenVectores {
        +string modelo
        +int dimension
        +vector~DocumentoIndexado~ documentos
        +cargar(path) bool
        +guardar(path) bool
        +masParecidos(vector, k) vector~Coincidencia~
    }
    class DocumentoIndexado {
        +string ruta
        +int64 mtime
        +uint64 tamano
        +string metodo
        +vector~Fragmento~ fragmentos
    }
    class Fragmento {
        +string texto
        +int pagina
        +vector~float~ vector
    }
    class ProveedorEmbeddings {
        <<interface>>
        +modelo() string
        +embeber(textos, consulta, vectores, error) bool
    }
    class ClienteEmbeddings {
        +embeber(textos, consulta, vectores, error) bool
    }
    class ServidorEmbeddings {
        -LocalInferenceManager servidor_
        +global()$ ServidorEmbeddings
        +asegurar(config, progreso, error) ProveedorEmbeddings
        +activo() ProveedorEmbeddings
        +detener()
    }
    class ExtractorTexto {
        <<funciones>>
        +extraerTexto(path) TextoExtraido
        +textoDePdf(bytes) string
        +xmlATexto(xml) string
    }
    class BuscarDocumentosTool {
        +execute(consulta, max) ToolResult
    }
    class SelectorColor {
        +Hsv hsv
        +Rgb original
        +Box cajaCuadro
        +Box cajaTono
        +fijarDesde(Zona, x, y)
        +mover(dx, dy)
        +dibujar() Element
    }

    BaseDocumentos *-- AlmacenVectores
    AlmacenVectores *-- DocumentoIndexado
    DocumentoIndexado *-- Fragmento
    BaseDocumentos ..> ExtractorTexto
    BaseDocumentos ..> ProveedorEmbeddings
    ProveedorEmbeddings <|.. ClienteEmbeddings
    ServidorEmbeddings ..> ClienteEmbeddings : crea
    ServidorEmbeddings --> LocalInferenceManager : modo embeddings
    BuscarDocumentosTool ..> BaseDocumentos
    AplicacionTui *-- SelectorColor
    SelectorColor ..> PreferenciasTui : fijarColor en vivo
```
