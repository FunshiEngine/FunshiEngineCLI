# Arquitectura UML - FunshiEngineCLI

Documentación de arquitectura en formato UML de la CLI del motor FunshiEngineGL
y de su agente Silvy. Complementa a [../ARQUITECTURA.md](../ARQUITECTURA.md)
(texto) y a [DiagramaClases.es.md](DiagramaClases.es.md) (clases).

Convenciones:
- **Idioma**: español (idioma principal del repositorio); versión en inglés en [ArquitecturaUML.eng.md](ArquitecturaUML.eng.md)
- **Formato**: Mermaid (diagramas directamente en Markdown)
- **Relaciones**: `-->` dependencia/llamada, `-.->` asíncrono o por callback, `==>` proceso externo

> Nota: los nombres con caracteres especiales (`::`, `()`, `/`) van entre comillas para Mermaid.

## 1. Diagrama de Capas

```mermaid
graph TD
    subgraph FRONT [Front-ends]
        direction LR
        MAIN["main.cpp<br/>JSON por stdin / archivo"]
        TUI["Tui/<br/>FTXUI: --tui, --chat"]
    end

    subgraph AGENTE [Agente Silvy]
        direction LR
        SILVY[Silvy]
        HARNESS[Harness]
        AUD[Auditoria]
        LLM["Agente/LLM/<br/>Bucle · Cliente · Config<br/>Herramientas · HuggingFace · Local"]
    end

    subgraph NUCLEO [Núcleo agnóstico del transporte]
        direction LR
        SOL[Solicitud]
        EJ["ejecutarSolicitud()"]
        RES[ResultadoComando]
        CAT[CatalogoComandos]
    end

    subgraph DOMINIO [Dominio]
        direction LR
        CMD["Comandos/<br/>Proyecto · Escena · Archivos · Scripts<br/>Assets · Config · Diagnosticos · Agente"]
        VAL[Validacion]
        PRY[Proyectos]
        ESC["Escena/<br/>ContextoEscena"]
    end

    subgraph MOTOR [Motor - submódulo extern/FunshiEngine]
        ENG["funshi_engine<br/>funshi_project_core<br/>funshi_diagnostics_core"]
    end

    MAIN --> EJ
    TUI --> EJ
    TUI --> SILVY
    SILVY --> HARNESS
    SILVY --> LLM
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

## 2. Diagrama de Componentes

```mermaid
graph LR
    subgraph CLI [Proceso FunshiCLI]
        TUI[Tui]
        SILVY[Silvy]
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

    LLAMA[("llama-server<br/>proceso hijo")]
    HFAPI[("huggingface.co<br/>API + archivos")]
    GH[("api.github.com<br/>releases llama.cpp")]
    REMOTO[("API remota<br/>OpenAI-compatible")]
    DISCO[("Disco<br/>.agentes/, modelos, llama-bin")]

    TUI --> SILVY
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

## 3. Diagramas de Secuencia

### 3.1 Petición JSON

```mermaid
sequenceDiagram
    actor Cliente as Script / CI / Motor
    participant Main as main.cpp
    participant Nucleo as ejecutarSolicitud()
    participant Cat as CatalogoComandos
    participant Cmd as Comandos/Dominio
    participant Val as Validacion
    participant Motor as funshi_engine

    Cliente->>Main: FunshiCLI --request-file peticion.json
    Main->>Main: parsear argv y JSON a Solicitud
    Main->>Nucleo: ejecutarSolicitud(solicitud)
    Nucleo->>Cat: comandoDesdeTexto(comando)
    Cat-->>Nucleo: ComandoCLI
    Nucleo->>Cmd: manejador(argumentos)
    Cmd->>Val: validarArgumentos(tabla)
    alt argumentos inválidos
        Val-->>Cmd: error de uso
        Cmd-->>Nucleo: ResultadoComando(fallo, código 2)
    else válidos
        Cmd->>Motor: operar (proyecto / escena / scripts)
        Motor-->>Cmd: datos
        Cmd-->>Nucleo: ResultadoComando(éxito, código 0)
    end
    Nucleo-->>Main: ResultadoComando
    Main-->>Cliente: JSON por stdout y código de salida
```

### 3.2 Chat con Silvy y llamada a herramienta

```mermaid
sequenceDiagram
    actor Usuario
    participant Chat as Tui/Chat
    participant Hilo as Hilo de trabajo
    participant Silvy
    participant Bucle as AgentLoop
    participant GW as LlmGateway
    participant Srv as llama-server
    participant Tool as EngineTool
    participant Harness
    participant Nucleo as ejecutarSolicitud()

    Usuario->>Chat: escribe orden y Enter
    Chat->>Hilo: std::thread(procesarOrdenChat)
    Hilo->>Silvy: procesar(texto, onEvent)
    alt texto empieza con /
        Silvy->>Harness: ejecutar(Solicitud)
        Harness->>Nucleo: ejecutarSolicitud()
        Nucleo-->>Silvy: ResultadoComando
    else lenguaje natural
        Silvy->>Bucle: run(historial, onEvent)
        loop hasta 8 rondas
            Bucle->>GW: chatStream(mensajes, tools)
            GW->>Srv: POST /v1/chat/completions (stream)
            Srv-->>GW: deltas SSE
            GW-->>Bucle: LlmChunk Text / ToolCall
            Bucle-->>Chat: AgentEvent Text (PostEvent, máx 30 fps)
            opt hay tool call
                Bucle->>Bucle: ToolCallSanitizer valida JSON
                Bucle-->>Chat: AgentEvent ToolStart
                Bucle->>Tool: execute(args)
                Tool->>Harness: ejecutar(Solicitud)
                Harness->>Nucleo: ejecutarSolicitud()
                Nucleo-->>Tool: ResultadoComando
                Tool-->>Bucle: ToolResult
                Bucle-->>Chat: AgentEvent ToolEnd
            end
        end
        Bucle-->>Silvy: AgentAnswer
    end
    Silvy-->>Hilo: RespuestaProcesada
    Hilo-->>Chat: finalizar mensaje y redibujar
```

### 3.3 Instalar y activar un modelo

```mermaid
sequenceDiagram
    actor Usuario
    participant Panel as Tui/ModelosHF
    participant Hilo as Hilo de trabajo
    participant HF as HfApi
    participant LIM as LocalInferenceManager
    participant Inst as InstaladorLlama
    participant Reg as RegistroServidor
    participant Srv as llama-server
    participant Silvy

    Usuario->>Panel: Enter en una variante
    Panel->>Hilo: iniciarAutoConfiguracionModelo(variante)
    Hilo->>LIM: downloadModel(variante)
    alt ya está en disco con el tamaño exacto
        LIM-->>Hilo: reutiliza el archivo
    else falta o está incompleto
        LIM->>HF: downloadVariant a .part (reanudable)
        HF-->>LIM: archivo verificado con SHA256
    end
    Hilo->>Inst: asegurarActualizado()
    Inst-->>Hilo: binario oficial (o el instalado si no hay red)
    Hilo->>LIM: startServer(modelo, puerto, contexto)
    LIM->>Reg: detenerServidorRegistrado()
    LIM->>Srv: nohup llama-server ... &
    loop cada 150 ms hasta 120 s
        LIM->>Srv: GET /health
    end
    LIM->>Reg: registrarServidor(pid, puerto)
    Hilo->>LIM: consultarCapacidades() por /props
    Hilo->>Silvy: configurarLlm(config, herramientasNativas)
    Hilo->>Hilo: LlmConfig::save()
    Hilo-->>Panel: estado Listo (PostEvent)
```

### 3.4 Salida de la TUI y señales

```mermaid
sequenceDiagram
    actor Usuario
    participant SO as Sistema operativo
    participant Tui as ejecutarTui()
    participant FTXUI as ScreenInteractive
    participant Reg as RegistroServidor
    participant Srv as llama-server

    Tui->>Reg: limpiarServidoresHuerfanos()
    Tui->>Reg: instalarCierreServidorPorSenal()
    Tui->>FTXUI: Loop(aplicacion)
    alt Esc, q o Ctrl+C
        Usuario->>FTXUI: tecla de salida
        FTXUI-->>Tui: Loop termina
        Tui->>Reg: detenerServidorRegistrado()
        Reg->>Srv: SIGTERM y SIGKILL tras 3 s
    else SIGHUP o SIGTERM
        SO->>FTXUI: señal
        FTXUI->>FTXUI: restaura la terminal y re-emite la señal
        FTXUI->>Reg: manejadorSenal()
        Reg->>Srv: SIGTERM
    end
```

### 3.5 Copiar el texto seleccionado

```mermaid
sequenceDiagram
    actor Usuario
    participant Raton as manejarRaton()
    participant Render as NodoSeleccion (render)
    participant UI as OnEvent Custom
    participant Hilo as Hilo de copia
    participant Clip as Clipboard
    participant Term as Terminal

    Usuario->>Raton: presiona, arrastra y suelta
    Raton->>Raton: guarda coordenadas (o palabra / línea con doble / triple clic)
    Render->>Render: pinta el resaltado y lee el texto de las celdas
    Render->>UI: PostEvent(Custom)
    UI->>Hilo: lanzarCopia(texto)
    Hilo->>Clip: copiar(texto)
    Clip-->>Hilo: ResultadoCopia (wl-copy / xclip / pbcopy / tmux / Win32)
    Hilo->>UI: PostEvent(Custom) con el resultado
    alt copia correcta
        UI-->>Usuario: aviso Copiado: N caracteres (método)
    else sin herramienta o falló
        UI->>Term: secuencia OSC 52 entre cuadros
        UI-->>Usuario: aviso Enviado a la terminal (OSC 52)
    end
```

## 4. Diagramas de Estados

### 4.1 Sección Modelos de la TUI

```mermaid
stateDiagram-v2
    state "LISTA_LOCALES" as Descargados
    state "LISTA_MODELOS" as Busqueda
    state "LISTA_VARIANTES" as Variantes
    state "CONFIGURANDO" as Configurando
    state "ERROR_CONFIG" as ErrorConfig

    [*] --> Descargados: entrar a la sección
    Descargados --> Busqueda: Tab
    Busqueda --> Descargados: Tab
    Busqueda --> Variantes: Enter en un resultado
    Variantes --> Busqueda: Esc
    Variantes --> Descargados: Tab o l
    Variantes --> Configurando: Enter en una variante
    Descargados --> Configurando: Enter en un modelo completo
    Configurando --> ErrorConfig: falla
    Configurando --> Descargados: Esc (si vino de Descargados)
    Configurando --> Variantes: Esc (si vino de Variantes)
    ErrorConfig --> Descargados: Esc
    ErrorConfig --> Variantes: Esc
    Descargados --> [*]: Esc

```

### 4.2 LocalInferenceManager

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> PREPARING_SERVER: ensureLlamaServer()
    PREPARING_SERVER --> IDLE: binario listo
    IDLE --> DOWNLOADING_MODEL: downloadModel()
    DOWNLOADING_MODEL --> IDLE: descargado y verificado
    IDLE --> STARTING_SERVER: startServer()
    STARTING_SERVER --> RUNNING: /health responde
    RUNNING --> STOPPING: stopServer()
    STOPPING --> IDLE
    PREPARING_SERVER --> ERROR
    DOWNLOADING_MODEL --> ERROR
    STARTING_SERVER --> ERROR: timeout o el proceso murió
    ERROR --> IDLE: reintento
```

## 5. Diagrama de Despliegue (procesos e hilos)

```mermaid
graph TB
    subgraph MAQUINA [Máquina del usuario]
        subgraph PROC [Proceso FunshiCLI]
            UI["Hilo de UI<br/>bucle FTXUI"]
            W1["Hilo: orden de chat"]
            W2["Hilo: búsqueda / variantes HF"]
            W3["Hilo: instalar modelo"]
            W4["Hilo: actualizar llama-server al iniciar"]
            W5["Hilo: lector de entrada de respaldo<br/>(solo pty de Windows)"]
        end
        LS["Proceso llama-server<br/>127.0.0.1:11434"]
        FS[("~/.agentes/local-inference<br/>llama-bin/, models/, llama-server.pid/.log")]
        CFG[(".agentes/llm-config.json<br/>metricas.jsonl, silvy-auditoria.log")]
    end
    HF[("huggingface.co")]
    GH[("api.github.com")]

    W1 -.->|PostEvent| UI
    W2 -.->|PostEvent| UI
    W3 -.->|PostEvent| UI
    W1 -->|HTTP SSE| LS
    W3 -->|lanza / registra PID| LS
    W2 --> HF
    W3 --> HF
    W4 --> GH
    W3 --> FS
    W4 --> FS
    UI --> CFG
```

## 6. Dependencias Externas

```mermaid
graph LR
    CLI[FunshiCLI]
    TESTS[cli-tests]
    CLI --> ENGINE["funshi_engine<br/>(submódulo)"]
    CLI --> FTXUI["FTXUI 7.0.3<br/>(FetchContent)"]
    CLI --> CURL[libcurl]
    CLI --> SSL["OpenSSL<br/>SHA256"]
    CLI --> JSON["nlohmann/json<br/>(vendorizado en el motor)"]
    CLI --> BULLET["Bullet<br/>(vía motor)"]
    TESTS -.->|ejecuta el binario| CLI
    TESTS --> JSON
```

## 7. Patrones de Diseño Identificados

| Patrón | Dónde | Para qué |
|---|---|---|
| **Command** (petición como objeto) | `Solicitud` → `ejecutarSolicitud()` | Un único camino para JSON, TUI y agente |
| **Enum + switch exhaustivo** | `ComandoCLI`, `NucleoCLI.cpp` | El compilador avisa si falta un comando |
| **Strategy** | `LlmGateway` (`OpenAiCompatibleGateway`) | Cambiar de backend LLM sin tocar el bucle |
| **Registry** | `ToolRegistry` | Catálogo de herramientas por nombre con caché de specs |
| **Template Method** | `EngineTool::executeCommand()` | Todas las tools pasan por `Harness` igual |
| **Factory** | `createEngineToolRegistry()` | Arma el registro de herramientas del motor |
| **Facade** | `Harness`, `LocalInferenceManager`, `HfApi` | Esconden auditoría, procesos y HTTP |
| **RAII** | `ContextoEscena`, `TempPruebas::CarpetaPrueba` | Construcción y destrucción ordenadas |
| **Pimpl** | `HfApi::Impl` (`HfApiImpl.h`) | Aísla cURL del header público |
| **Observer (callbacks)** | `AgentEvent`, `ProgressCallback`, `PostEvent` | La TUI se entera del progreso sin acoplarse |
| **Tabla de validación** | `RequisitoArg` | Argumentos declarados, no programados |
