# FunshiEngineCLI

Interfaz de línea de comandos del motor [FunshiEngineGL](https://github.com/FunshiEngine/FunshiEngineGL).

Ejecuta operaciones del motor mediante solicitudes JSON sin abrir el editor
gráfico: creación y gestión de proyectos, escenas y objetos, importación de
assets, compilación de scripts y consulta del registro de diagnósticos.

El código está modularizado en un núcleo reutilizable agnóstico del
transporte (`src/Nucleo`), preparado para que una futura app de escritorio
reutilice la misma lógica sin pasar por stdin/stdout.

## Compilación

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

El ejecutable queda en `build/FunshiCLI` (Linux) o `build/FunshiCLI.exe`
(Windows).

## Uso rápido

```bash
FunshiCLI help
FunshiCLI version
FunshiCLI --request < solicitud.json
FunshiCLI --request-file solicitud.json
```

Ejemplo — crear un proyecto 2D:

```bash
echo '{"command":"project.create","args":{"name":"MiJuego2D","dimension":2}}' \
  | FunshiCLI --request
```

Respuesta:

```json
{"apiVersion":1,"error":null,"ok":true,"result":{"dimension":2,"name":"MiJuego2D","path":".../MotorGrafico/Proyects/MiJuego2D"}}
```

## Documentación

- [MANUAL_DE_USO.md](MANUAL_DE_USO.md) — referencia completa de comandos,
  argumentos, formatos de respuesta y códigos de salida.
- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) — arquitectura por capas y
  relación con el motor.

## Tests

```bash
cd build && ctest -R cli-tests --output-on-failure
```

## Licencia

Apache License 2.0 — ver [LICENSE](LICENSE).
