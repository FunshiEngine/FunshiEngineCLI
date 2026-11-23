# Documentación — FunshiEngineCLI

| Documento | Tipo | Contenido |
|---|---|---|
| [ARQUITECTURA.md](ARQUITECTURA.md) | Arquitectura | Capas y regla de dependencias, módulos, flujos, concurrencia, datos en disco, errores, puntos de extensión y convenciones |
| [uml/ArquitecturaUML.es.md](uml/ArquitecturaUML.es.md) · [eng](uml/ArquitecturaUML.eng.md) | UML | Capas, componentes, secuencias (petición JSON, chat con herramientas, instalación de modelo, salida y señales, copia al portapapeles, delegación en subagentes), estados, despliegue, dependencias y patrones |
| [uml/DiagramaClases.es.md](uml/DiagramaClases.es.md) · [eng](uml/DiagramaClases.eng.md) | UML | Clases por módulo: núcleo, dominio, agente, LLM (bucle, herramientas, cliente), inferencia local y Hugging Face, conocimiento, subagentes y sesiones, TUI |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | Estructura | Árbol de archivos, decisiones de diseño, relación con el motor y multiplataforma |
| [MANUAL_DE_USO.md](MANUAL_DE_USO.md) | Uso | Referencia de comandos, argumentos, respuestas, códigos de salida y la TUI |

Guía para agentes de código y convenciones del repo: [../AGENTS.md](../AGENTS.md).

## Diagramas

Los diagramas están en [Mermaid](https://mermaid.js.org/) dentro del Markdown:
GitHub, GitLab y la mayoría de los editores (VS Code con la extensión de
Mermaid, IDEs de JetBrains) los muestran sin pasos extra. Mismas convenciones
que la documentación UML del motor (`FunshiEngineOpenGL/doc/uml/`): una
versión `.es.md` (principal) y una `.eng.md`.

Al cambiar la arquitectura (módulo nuevo, flujo nuevo, clase pública nueva)
se actualizan `ARQUITECTURA.md` y el diagrama afectado en las dos versiones.
