# Diagrama de Clases - FunshiEngineCLI

Clases y estructuras principales de la CLI, agrupadas por módulo. Cada
diagrama muestra la API pública relevante (no todos los miembros privados).
Arquitectura general en [ArquitecturaUML.es.md](ArquitecturaUML.es.md).

Convenciones:
- **Idioma**: español; versión en inglés en [DiagramaClases.eng.md](DiagramaClases.eng.md)
- **Formato**: Mermaid `classDiagram`
- **Relaciones**: `<|--` herencia, `*--` composición, `o--` agregación, `-->` asociación, `..>` dependencia
- Las funciones libres de un módulo se agrupan en una clase con estereotipo `<<funciones>>`

## 1. Núcleo: petición, respuesta y dispatch

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
        ...48 comandos
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
        +... un manejador por comando
    }

    ResultadoComando *-- Respuesta
    NucleoCLI ..> Solicitud : recibe
    NucleoCLI ..> ResultadoComando : devuelve
    NucleoCLI ..> CatalogoComandos : traduce
    NucleoCLI ..> ComandoCLI : switch exhaustivo
    NucleoCLI ..> Comandos : delega
```

## 2. Dominio: escena, validación y proyectos

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

## 3. Agente: Silvy, Harness y Auditoría

```mermaid
classDiagram
    class Silvy {
        -Harness harness_
        -shared_ptr~LlmGateway~ gateway_
        -shared_ptr~ToolRegistry~ registry_
        -shared_ptr~AgentLoop~ agentLoop_
        -vector~ChatMessage~ conversationHistory_
        +configurarLlm(LlmConfig, bool nativeToolCalls) bool
        +tieneLlm() bool
        +procesar(string texto, onEvent) RespuestaProcesada
        +limpiarHistorialLlm()
        +modeloActivo() string
        -ejecutarComandoCli(string) RespuestaProcesada
        -enviarAlLlm(string, onEvent) RespuestaProcesada
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
    Silvy --> AgentLoop
    Silvy ..> RespuestaProcesada
    Harness --> Auditoria
    Auditoria *-- EntradaAuditoria
    Harness ..> NucleoCLI : ejecutarSolicitud()
```

## 4. Agente/LLM: bucle, herramientas y cliente

```mermaid
classDiagram
    class AgentLoop {
        -shared_ptr~LlmGateway~ gateway_
        -shared_ptr~ToolRegistry~ registry_
        -bool nativeToolCalls_
        +int maxIterations = 8
        +run(vector~ChatMessage~ mensajes, onEvent) AgentAnswer
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
    class LlmGateway {
        <<interface>>
        +chatStream(mensajes, tools, onChunk)* bool
        +chatText(mensajes)* string
        +chat(mensajes)* string
        +listModels()* vector~string~
        +close()*
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
        +string name
        +string argsJson
        +bool ok
        +string result
    }
    class AgentAnswer {
        +string text
        +vector~ExecutedTool~ toolCalls
    }

    AgentLoop --> LlmGateway
    AgentLoop --> ToolRegistry
    AgentLoop ..> ToolCallSanitizer
    AgentLoop ..> PromptToolParser : modo prompt
    AgentLoop ..> AgentEvent : emite
    AgentLoop ..> AgentAnswer : devuelve
    ToolRegistry o-- Tool
    Tool <|-- EngineTool
    EngineTool <|-- SceneObjectCreateTool
    EngineTool <|-- ProjectListTool
    EngineTool <|-- ScriptCompileTool
    EngineTool --> Harness
    LlmGateway <|.. OpenAiCompatibleGateway
    OpenAiCompatibleGateway ..> LlmException : lanza
    LlmGateway ..> ChatMessage
    Tool ..> ToolSpec
```

> Hay 23 herramientas `EngineTool` (proyecto, escena, archivos, scripts,
> assets, diagnósticos); el diagrama muestra tres. Todas las crea
> `createEngineToolRegistry(harness)`.

## 5. Agente/LLM: inferencia local y Hugging Face

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

## 6. TUI

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
        -vector~MensajeChat~ chatMensajes_
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
        +int version
        +vector~LineaChat~ lineasCache
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
    AplicacionTui *-- MensajeChat
    AplicacionTui *-- EstadoSeccion
    AplicacionTui o-- EstadoCompartido : compartido con hilos
    AplicacionTui ..> AyudaComandos
    AplicacionTui ..> LocalInferenceManager
    AplicacionTui ..> HfApi
    InterfazTui ..> AplicacionTui : crea
    InterfazTui ..> EntradaRespaldo
    InterfazTui ..> RegistroServidor : salida
    AplicacionTui ..> Clipboard : copia la selección
    Clipboard ..> ResultadoCopia
```
