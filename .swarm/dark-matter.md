## Dark Matter: Hidden Couplings

Found 17 file pairs that frequently co-change but have no import relationship:

| File A | File B | NPMI | Co-Changes | Lift |
|--------|--------|------|------------|------|
| src/Comandos/ComandosEscena.cpp | src/Comandos/ComandosEscena.h | 1.000 | 4 | 7.75 |
| src/Comandos/ComandosEscena.cpp | src/Nucleo/CatalogoComandos.cpp | 1.000 | 4 | 7.75 |
| src/Comandos/ComandosEscena.cpp | src/Nucleo/CatalogoComandos.h | 1.000 | 4 | 7.75 |
| src/Comandos/ComandosEscena.h | src/Nucleo/CatalogoComandos.cpp | 1.000 | 4 | 7.75 |
| src/Comandos/ComandosEscena.h | src/Nucleo/CatalogoComandos.h | 1.000 | 4 | 7.75 |
| src/Nucleo/CatalogoComandos.cpp | src/Nucleo/CatalogoComandos.h | 1.000 | 4 | 7.75 |
| src/Tui/InterfazTui.cpp | src/Tui/InterfazTui.h | 0.877 | 3 | 7.75 |
| PROJECT_STRUCTURE.md | README.md | 0.816 | 5 | 4.43 |
| src/Comandos/ComandosEscena.cpp | src/Nucleo/NucleoCLI.cpp | 0.802 | 4 | 5.17 |
| src/Comandos/ComandosEscena.h | src/Nucleo/NucleoCLI.cpp | 0.802 | 4 | 5.17 |
| src/Nucleo/CatalogoComandos.cpp | src/Nucleo/NucleoCLI.cpp | 0.802 | 4 | 5.17 |
| src/Nucleo/CatalogoComandos.h | src/Nucleo/NucleoCLI.cpp | 0.802 | 4 | 5.17 |
| src/Tui/InterfazTui.h | src/main.cpp | 0.781 | 3 | 6.20 |
| MANUAL_DE_USO.md | README.md | 0.696 | 7 | 2.82 |
| src/Tui/InterfazTui.cpp | src/main.cpp | 0.658 | 3 | 4.65 |
| src/main.cpp | tests/CliTests.cpp | 0.658 | 3 | 4.65 |
| MANUAL_DE_USO.md | PROJECT_STRUCTURE.md | 0.568 | 5 | 2.82 |

These pairs likely share an architectural concern invisible to static analysis.
Consider adding explicit documentation or extracting the shared concern.