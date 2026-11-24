# UML Architecture - FunshiEngineCLI

UML architecture documentation for the FunshiEngineGL engine CLI and its
Silvy agent. Complements [../ARQUITECTURA.md](../ARQUITECTURA.md) (prose,
Spanish) and [DiagramaClases.eng.md](DiagramaClases.eng.md) (classes).

Conventions:
- **Language**: English; the primary (Spanish) version is [ArquitecturaUML.es.md](ArquitecturaUML.es.md)
- **Format**: Mermaid (diagrams embedded in Markdown)
- **Relations**: `-->` dependency/call, `-.->` asynchronous or via callback, `==>` external process

> Note: names with special characters (`::`, `()`, `/`) are quoted for Mermaid.

## 1. Layer Diagram

```mermaid
graph TD
    subgraph FRONT [Front-ends]
        direction LR
        MAIN["main.cpp<br/>JSON via stdin / file"]
        TUI["Tui/<br/>FTXUI: --tui, --chat"]
    end

    subgraph AGENTE [Silvy agent]
        direction LR
        SILVY[Silvy]
        HARNESS[Harness]
        AUD[Auditoria]
        LLM["Agente/LLM/<br/>Bucle · Cliente · Config<br/>Herramientas · HuggingFace · Local"]
        CON["Knowledge (RAG)<br/>Subagents · Sessions"]
    end

    subgraph NUCLEO [Transport-agnostic core]
        direction LR
        SOL[Solicitud]
        EJ["ejecutarSolicitud()"]
        RES[ResultadoComando]
        CAT[CatalogoComandos]
    end

    subgraph DOMINIO [Domain]
        direction LR
        CMD["Comandos/<br/>Proyecto · Escena · Archivos · Scripts<br/>Assets · Config · Diagnosticos · Agente"]
        VAL[Validacion]
        PRY[Proyectos]
        ESC["Escena/<br/>ContextoEscena"]
    end

    subgraph MOTOR [Engine - extern/FunshiEngine submodule]
        ENG["funshi_engine<br/>funshi_project_core<br/>funshi_diagnostics_core"]
    end

    MAIN --> EJ
    TUI --> EJ
    TUI --> SILVY
    SILVY --> HARNESS
    SILVY --> LLM
    SILVY --> CON
    LLM -->|EngineTool| HARNESS
    HARNESS --> EJ
    HARNESS --> AUD
    EJ --> CAT
    EJ --> CMD
    CMD --> VAL
    CMD --> PRY
    CMD --> ESC
    PRY --> ENG
    ESC --> ENG
    CMD --> ENG
```

## 2. Component Diagram

```mermaid
graph LR
    subgraph CLI [FunshiCLI process]
        TUI[Tui]
        SES[AlmacenSesiones]
        SILVY[Silvy]
        RAG[BaseConocimiento]
        SUB[GestorSubagentes]
        LOOP[AgentLoop]
        REG[ToolRegistry]
        TOOLS[EngineTool x N]
        GW[LlmGateway]
        HF[HfApi]
        LIM[LocalInferenceManager]
        INST[InstaladorLlama]
        RSV[RegistroServidor]
        HTTP[HttpDescarga]
        CFG[LlmConfig]
        NUC[Nucleo + Comandos]
    end

    LLAMA[("llama-server<br/>child process")]
    HFAPI[("huggingface.co<br/>API + files")]
    GH[("api.github.com<br/>releases llama.cpp")]
    REMOTO[("Remote API<br/>OpenAI-compatible")]
    DISCO[("Disk<br/>.agentes/, models, llama-bin")]

    TUI --> SILVY
    TUI --> SES
    SES --> DISCO
    SILVY --> RAG
    SILVY --> SUB
    SUB -->|one loop per task| LOOP
    REG -->|buscar_comandos| RAG
    TUI --> HF
    TUI --> LIM
    SILVY --> LOOP
    LOOP --> GW
    LOOP --> REG
    REG --> TOOLS
    TOOLS --> NUC
    GW ==>|HTTP + SSE| LLAMA
    GW ==>|HTTP + SSE| REMOTO
    HF --> HTTP
    HTTP ==> HFAPI
    LIM --> INST
    LIM --> RSV
    LIM --> HF
    INST --> HTTP
    HTTP ==> GH
    LIM ==>|nohup / kill| LLAMA
    CFG --> DISCO
    RSV --> DISCO
    INST --> DISCO
```

## 3. Sequence Diagrams

### 3.1 JSON request

```mermaid
sequenceDiagram
    actor Cliente as Script / CI / Engine
    participant Main as main.cpp
    participant Nucleo as ejecutarSolicitud()
    participant Cat as CatalogoComandos
    participant Cmd as Comandos/Dominio
    participant Val as Validacion
    participant Motor as funshi_engine

    Cliente->>Main: FunshiCLI --request-file peticion.json
    Main->>Main: parse argv and JSON into Solicitud
    Main->>Nucleo: ejecutarSolicitud(solicitud)
    Nucleo->>Cat: comandoDesdeTexto(comando)
    Cat-->>Nucleo: ComandoCLI
    Nucleo->>Cmd: manejador(argumentos)
    Cmd->>Val: validarArgumentos(tabla)
    alt invalid arguments
        Val-->>Cmd: usage error
        Cmd-->>Nucleo: ResultadoComando(failure, code 2)
    else valid
        Cmd->>Motor: operate (project / scene / scripts)
        Motor-->>Cmd: data
        Cmd-->>Nucleo: ResultadoComando(success, code 0)
    end
    Nucleo-->>Main: ResultadoComando
    Main-->>Cliente: JSON on stdout and exit code
```

### 3.2 Chat with Silvy and a tool call

```mermaid
sequenceDiagram
    actor Usuario as User
    participant Chat as Tui/Chat
    participant Hilo as Worker thread
    participant Mon as MonitorSesion
    participant Silvy
    participant Bucle as AgentLoop
    participant GW as LlmGateway
    participant Srv as llama-server
    participant Tool as EngineTool
    participant Harness
    participant Nucleo as ejecutarSolicitud()

    Usuario->>Chat: types an order and Enter
    Chat->>Hilo: std::thread(procesarOrdenChat, sesion)
    Hilo->>Silvy: procesar(texto, sesion.agente, onEvent)
    alt text starts with /
        Silvy->>Harness: ejecutar(Solicitud)
        Harness->>Nucleo: ejecutarSolicitud()
        Nucleo-->>Silvy: ResultadoComando
    else natural language
        Silvy->>Silvy: BaseConocimiento.contextoParaPedido(texto)
        Silvy-->>Mon: AgentEvent Estado (retrieved documentation)
        Silvy->>Bucle: run(history, onEvent, documentation)
        loop up to 10 rounds
            Bucle-->>Mon: AgentEvent Estado round N (waiting for the model)
            Bucle->>GW: chatStream(mensajes, tools)
            GW->>Srv: POST /v1/chat/completions (stream)
            Srv-->>GW: SSE deltas
            GW-->>Bucle: LlmChunk Text / Reasoning / ToolCall
            Bucle->>Bucle: SeparadorPensamiento splits out text inside think
            Bucle-->>Mon: AgentEvent Pensamiento and Text
            Bucle-->>Mon: AgentEvent Estado round N (time and tokens)
            opt there is a tool call
                Bucle->>Bucle: ToolCallSanitizer validates JSON
                Bucle-->>Mon: AgentEvent ToolStart (previous text becomes Piensa)
                Bucle->>Tool: execute(args)
                Tool->>Harness: ejecutar(Solicitud)
                Harness->>Nucleo: ejecutarSolicitud()
                Nucleo-->>Tool: ResultadoComando
                Tool-->>Bucle: ToolResult
                Bucle-->>Mon: AgentEvent ToolEnd (result and detail)
            end
        end
        Bucle-->>Silvy: AgentAnswer
    end
    Mon-->>Chat: messages in order (PostEvent, max 30 fps)
    Silvy-->>Hilo: RespuestaProcesada
    Hilo->>Mon: cerrar(final text)
    Hilo->>Hilo: AlmacenSesiones.guardar(sesion)
```

### 3.3 Install and activate a model

```mermaid
sequenceDiagram
    actor Usuario as User
    participant Panel as Tui/ModelosHF
    participant Hilo as Worker thread
    participant HF as HfApi
    participant LIM as LocalInferenceManager
    participant Inst as InstaladorLlama
    participant Reg as RegistroServidor
    participant Srv as llama-server
    participant Silvy

    Usuario->>Panel: Enter on a variant
    Panel->>Hilo: iniciarAutoConfiguracionModelo(variante)
    Hilo->>LIM: downloadModel(variante)
    alt already on disk with the exact size
        LIM-->>Hilo: reuses the file
    else missing or incomplete
        LIM->>HF: downloadVariant to .part (resumable)
        HF-->>LIM: file verified with SHA256
    end
    Hilo->>Inst: asegurarActualizado()
    Inst-->>Hilo: official binary (or the installed one when offline)
    Hilo->>LIM: startServer(model, port, context)
    LIM->>Reg: detenerServidorRegistrado()
    LIM->>Srv: nohup llama-server ... &
    loop every 150 ms up to 120 s
        LIM->>Srv: GET /health
    end
    LIM->>Reg: registrarServidor(pid, puerto)
    Hilo->>LIM: consultarCapacidades() via /props
    Hilo->>Silvy: configurarLlm(config, herramientasNativas)
    Hilo->>Hilo: LlmConfig::save()
    Hilo-->>Panel: Ready state (PostEvent)
```

### 3.4 TUI exit and signals

```mermaid
sequenceDiagram
    actor Usuario as User
    participant SO as Operating system
    participant Tui as ejecutarTui()
    participant FTXUI as ScreenInteractive
    participant Reg as RegistroServidor
    participant Srv as llama-server

    Tui->>Reg: limpiarServidoresHuerfanos()
    Tui->>Reg: instalarCierreServidorPorSenal()
    Tui->>FTXUI: Loop(aplicacion)
    alt Esc, q or Ctrl+C
        Usuario->>FTXUI: exit key
        FTXUI-->>Tui: Loop returns
        Tui->>Reg: detenerServidorRegistrado()
        Reg->>Srv: SIGTERM, then SIGKILL after 3 s
    else SIGHUP or SIGTERM
        SO->>FTXUI: signal
        FTXUI->>FTXUI: restores the terminal and re-raises the signal
        FTXUI->>Reg: manejadorSenal()
        Reg->>Srv: SIGTERM
    end
```

### 3.5 Copy the selected text

```mermaid
sequenceDiagram
    actor Usuario as User
    participant Raton as manejarRaton()
    participant Render as NodoSeleccion (render)
    participant UI as OnEvent Custom
    participant Hilo as Copy thread
    participant Clip as Clipboard
    participant Term as Terminal

    Usuario->>Raton: presses, drags and releases
    Raton->>Raton: stores coordinates (or word / line on double / triple click)
    Render->>Render: paints the highlight and reads the text from the cells
    Render->>UI: PostEvent(Custom)
    UI->>Hilo: lanzarCopia(texto)
    Hilo->>Clip: copiar(texto)
    Clip-->>Hilo: ResultadoCopia (wl-copy / xclip / pbcopy / tmux / Win32)
    Hilo->>UI: PostEvent(Custom) with the result
    alt copy succeeded
        UI-->>Usuario: toast Copied: N characters (method)
    else no tool or it failed
        UI->>Term: OSC 52 sequence between frames
        UI-->>Usuario: toast Sent to the terminal (OSC 52)
    end
```

### 3.6 Delegating to subagents

```mermaid
sequenceDiagram
    participant Bucle as main AgentLoop
    participant Tool as LanzarSubagentesTool
    participant Gestor as GestorSubagentes
    participant Sub as subagent AgentLoop
    participant RAG as BaseConocimiento
    participant Mon as MonitorSesion
    participant Chat as Tui/Chat

    Bucle->>Tool: lanzar_subagentes(tareas)
    Tool->>Tool: leerTareas validates 1 to 4 tasks, instructions and profile
    Tool->>Gestor: ejecutar(tareas, onEvent)
    loop each task, in batches of subagentesParalelos
        Gestor->>Mon: SubagenteInicio (id, full task)
        Mon->>Mon: creates SubagenteTui and the summary line in the session
        Gestor->>Sub: new loop with profile, own gateway and no lanzar_subagentes
        Gestor->>RAG: contextoParaPedido(instructions and context)
        Gestor->>Sub: run with instruccionesSubagente and documentation
        Sub->>Mon: all its events with origen = id
        Mon->>Mon: they go to the subagent transcript and update the summary
        Sub-->>Gestor: report RESULTADO / HECHO / PROBLEMAS
        Gestor->>Mon: SubagenteFin (report)
    end
    Chat->>Chat: click on the summary or /sub N opens the subagent view
    Gestor-->>Tool: trimmed reports
    Tool-->>Bucle: summary of the reports
    Bucle->>Bucle: verifies and answers the user
```

### 3.7 User documents: indexing and retrieval

```mermaid
sequenceDiagram
    participant TUI as AplicacionTui
    participant Hilo as Background thread
    participant SE as ServidorEmbeddings
    participant HF as HfApi
    participant Emb as llama-server --embedding
    participant Base as BaseDocumentos
    participant Ext as ExtractorTexto
    participant FVDB as AlmacenVectores
    participant Silvy

    TUI->>Hilo: iniciarDocumentos on start or with /docs actualizar
    Hilo->>Base: prepararCarpeta and archivos
    alt there are documents
        Hilo->>SE: asegurar(config)
        opt the model is missing
            SE->>HF: getVariants and downloadVariant (SHA256)
        end
        SE->>Emb: launch on port 11500 with registry embeddings.pid
        Hilo->>Base: actualizar(proveedor)
        loop each new or changed file
            Base->>Ext: extraerTexto (pdftotext, ZIP+XML, HTML, text)
            Base->>Base: fragmentar with page
            Base->>Emb: POST /v1/embeddings in batches
        end
        Base->>FVDB: save documentos.fvdb
        Hilo-->>TUI: notice in the status bar
    end
    Silvy->>Base: contextoParaPedido(request)
    Base->>Emb: query embedding (with instruction)
    Base->>FVDB: masParecidos (cosine)
    Base-->>Silvy: fragments above the threshold, with file and page
```

## 4. State Diagrams

### 4.1 TUI Models section

```mermaid
stateDiagram-v2
    state "LISTA_LOCALES" as Descargados
    state "LISTA_MODELOS" as Busqueda
    state "LISTA_VARIANTES" as Variantes
    state "CONFIGURANDO" as Configurando
    state "ERROR_CONFIG" as ErrorConfig

    [*] --> Descargados: enter the section
    Descargados --> Busqueda: Tab
    Busqueda --> Descargados: Tab
    Busqueda --> Variantes: Enter on a result
    Variantes --> Busqueda: Esc
    Variantes --> Descargados: Tab or l
    Variantes --> Configurando: Enter on a variant
    Descargados --> Configurando: Enter on a complete model
    Configurando --> ErrorConfig: fails
    Configurando --> Descargados: Esc (if it came from Downloaded)
    Configurando --> Variantes: Esc (if it came from Variants)
    ErrorConfig --> Descargados: Esc
    ErrorConfig --> Variantes: Esc
    Descargados --> [*]: Esc

```

### 4.2 LocalInferenceManager

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> PREPARING_SERVER: ensureLlamaServer()
    PREPARING_SERVER --> IDLE: binary ready
    IDLE --> DOWNLOADING_MODEL: downloadModel()
    DOWNLOADING_MODEL --> IDLE: downloaded and verified
    IDLE --> STARTING_SERVER: startServer()
    STARTING_SERVER --> RUNNING: /health answers
    RUNNING --> STOPPING: stopServer()
    STOPPING --> IDLE
    PREPARING_SERVER --> ERROR
    DOWNLOADING_MODEL --> ERROR
    STARTING_SERVER --> ERROR: timeout or the process died
    ERROR --> IDLE: retry
```

## 5. Deployment Diagram (processes and threads)

```mermaid
graph TB
    subgraph MAQUINA [User's machine]
        subgraph PROC [FunshiCLI process]
            UI["UI thread<br/>FTXUI loop"]
            W1["Threads: one order per session<br/>(several at once)"]
            W6["Threads: subagents<br/>(in batches)"]
            W2["Thread: HF search / variants"]
            W3["Thread: install model"]
            W4["Thread: update llama-server at startup"]
            W5["Thread: fallback input reader<br/>(Windows pty only)"]
        end
        LS["llama-server process<br/>127.0.0.1:11434"]
        FS[("~/.agentes/local-inference<br/>llama-bin/, models/, llama-server.pid/.log")]
        CFG[(".agentes/llm-config.json, sesiones/, conocimiento/<br/>metricas.jsonl, silvy-auditoria.log")]
    end
    HF[("huggingface.co")]
    GH[("api.github.com")]

    W1 -.->|PostEvent| UI
    W1 --> W6
    W6 -->|HTTP SSE| LS
    W2 -.->|PostEvent| UI
    W3 -.->|PostEvent| UI
    W1 -->|HTTP SSE| LS
    W3 -->|launches / records PID| LS
    W2 --> HF
    W3 --> HF
    W4 --> GH
    W3 --> FS
    W4 --> FS
    UI --> CFG
```

## 6. External Dependencies

```mermaid
graph LR
    CLI[FunshiCLI]
    TESTS[cli-tests]
    CLI --> ENGINE["funshi_engine<br/>(submodule)"]
    CLI --> FTXUI["FTXUI 7.0.3<br/>(FetchContent)"]
    CLI --> CURL[libcurl]
    CLI --> SSL["OpenSSL<br/>SHA256"]
    CLI --> JSON["nlohmann/json<br/>(vendored in the engine)"]
    CLI --> BULLET["Bullet<br/>(via engine)"]
    TESTS -.->|runs the binary| CLI
    TESTS --> JSON
```

## 7. Identified Design Patterns

| Pattern | Where | Purpose |
|---|---|---|
| **Command** (request as object) | `Solicitud` → `ejecutarSolicitud()` | A single path for JSON, TUI and agent |
| **Enum + exhaustive switch** | `ComandoCLI`, `NucleoCLI.cpp` | The compiler warns about a missing command |
| **Strategy** | `LlmGateway` (`OpenAiCompatibleGateway`) | Swap the LLM backend without touching the loop |
| **Registry** | `ToolRegistry` | Tool catalog by name with cached specs |
| **RAG (BM25)** | `BaseConocimiento`, `IndiceBm25` | Documentation of the relevant commands for each request |
| **Orchestrator–workers** | `GestorSubagentes` | The main agent hands out independent tasks |
| **Repository** | `AlmacenSesiones` | Session persistence behind a simple interface |
| **Template Method** | `EngineTool::executeCommand()` | Every tool goes through `Harness` the same way |
| **Factory** | `createEngineToolRegistry()` | Builds the engine tool registry |
| **Facade** | `Harness`, `LocalInferenceManager`, `HfApi` | Hide auditing, processes and HTTP |
| **RAII** | `ContextoEscena`, `TempPruebas::CarpetaPrueba` | Ordered construction and destruction |
| **Pimpl** | `HfApi::Impl` (`HfApiImpl.h`) | Keeps cURL out of the public header |
| **Observer (callbacks)** | `AgentEvent`, `ProgressCallback`, `PostEvent` | The TUI learns about progress without coupling |
| **Validation table** | `RequisitoArg` | Arguments are declared, not hand-coded |
