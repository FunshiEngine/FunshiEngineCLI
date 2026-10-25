cmake_minimum_required(VERSION 3.15)

project(FunshiEngineCLI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# El motor se construye desde el submódulo y no desde un build suelto de otro
# checkout: las cabeceras que compila la CLI y la biblioteca que enlaza tienen
# que salir del MISMO commit. Si divergen, los structs que cruzan la frontera
# (por ejemplo ConfigPersistence::Proyecto, que se devuelve por valor) tienen
# layouts distintos y el proceso se corrompe al liberar la memoria.
# BUILD_RUNTIME=ON construye solo la biblioteca del motor: la CLI no necesita
# el ejecutable del editor ni las pruebas headless del motor (esas viven en su
# propio repo).
set(BUILD_ENGINE ON CACHE BOOL "" FORCE)
set(BUILD_FUNSHI_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_RUNTIME ON CACHE BOOL "" FORCE)
# El motor activa ASan/UBSan en cualquier build con GCC/Clang. Con el GCC de
# MSYS2 esos runtimes no linkean, así que aquí se desactivan.
set(ENABLE_ASAN OFF CACHE BOOL "" FORCE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/CMakeLists.txt")
    add_subdirectory(extern/FunshiEngine/FunshiEngineGL EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(funshi_engine
        GIT_REPOSITORY https://github.com/FunshiEngine/FunshiEngineOpenGL.git
        GIT_TAG develop
    )
    FetchContent_MakeAvailable(funshi_engine)
endif()

find_package(Bullet QUIET)
if(Bullet_FOUND)
    include_directories(${BULLET_INCLUDE_DIRS})
else()
    if(EXISTS "/mingw64/include/bullet")
        include_directories("/mingw64/include/bullet")
    endif()
endif()

find_package(OpenGL REQUIRED)
find_package(GLFW3 QUIET)
find_package(assimp QUIET)

# FTXUI: librería TUI (C++17, MIT) para la interfaz interactiva de terminal.
# Tag fijo para builds reproducibles; solo se construyen los targets que la
# CLI consume.
include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v7.0.3
)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ftxui)

add_executable(FunshiCLI
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/Respuesta.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/Registro.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/CatalogoComandos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/NucleoCLI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Validacion/ValidacionArgs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Proyectos/ResolucionProyectos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Escena/ContextoEscena.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Escena/SerializacionEscena.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comandos/ComandosProyecto.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comandos/ComandosEscena.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comandos/ComandosAssets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comandos/ComandosScripts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comandos/ComandosDiagnosticos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comandos/ComandosArchivos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Tui/InterfazTui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Tui/EntradaRespaldo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Agente/Auditoria.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Agente/Harness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Agente/Silvy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Agente/ChatInteractivo.cpp
)

target_include_directories(FunshiCLI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/External
    # El parser de secuencias de terminal de FTXUI es interno (vive en su
    # árbol de fuentes, no en include/) y es el que traduce las teclas a
    # eventos; la TUI lo reutiliza para su lectura de respaldo en
    # terminales sin consola de Windows.
    ${ftxui_SOURCE_DIR}/src
)

target_compile_definitions(FunshiCLI PRIVATE
    FUNSHI_VERSION="0.5.4"
)

target_link_libraries(FunshiCLI PRIVATE
    funshi_project_core
    funshi_diagnostics_core
    ftxui::screen
    ftxui::dom
    ftxui::component
)

# El motor llega como target `funshi_engine` que construye el submódulo: es
# la única forma de que las cabeceras y la biblioteca sean del mismo commit.
target_link_libraries(FunshiCLI PRIVATE funshi_engine)

if(GLFW3_FOUND)
    target_link_libraries(FunshiCLI PRIVATE glfw)
endif()
target_link_libraries(FunshiCLI PRIVATE OpenGL::GL)

target_link_libraries(FunshiCLI PRIVATE
    BulletDynamics
    BulletCollision
    LinearMath
)
if(assimp_FOUND)
    target_link_libraries(FunshiCLI PRIVATE assimp::assimp)
else()
    target_link_libraries(FunshiCLI PRIVATE assimp)
endif()

# En Windows con MinGW, las DLLs de runtime no llegan al directorio de salida
# por sí solas: las del toolchain (libstdc++, gcc, pthread) y las del motor
# (assimp, glfw, bullet) viven en el directorio del compilador y sin ellas el
# proceso termina al arrancar, antes de imprimir nada. Se copian junto al
# ejecutable en cada compilación para que funcione desde cualquier terminal
# sin preparar el PATH. Cada una se copia solo si existe en ese directorio
# (una instalación incompleta no rompe la configuración).
if(WIN32 AND MINGW)
    get_filename_component(FUNSHI_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(FUNSHI_DLL IN ITEMS
            libstdc++-6.dll
            libgcc_s_seh-1.dll
            libwinpthread-1.dll
            libassimp-6.dll
            # Dependencias transitivas de assimp (minizip descomprime con
            # zlib y bzip2); sin ellas el arranque falla con 0xC0000135.
            libminizip-1.dll
            zlib1.dll
            libbz2-1.dll
            glfw3.dll
            libBulletCollision.dll
            libBulletDynamics.dll
            libLinearMath.dll
    )
        if(EXISTS "${FUNSHI_MINGW_BIN}/${FUNSHI_DLL}")
            add_custom_command(TARGET FunshiCLI POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${FUNSHI_MINGW_BIN}/${FUNSHI_DLL}"
                        "$<TARGET_FILE_DIR:FunshiCLI>"
                COMMENT "Copiando ${FUNSHI_DLL} junto a FunshiCLI"
            )
        endif()
    endforeach()
endif()

enable_testing()
add_executable(cli-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/CliTests.cpp
)
target_include_directories(cli-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL
    # JSON vendoriado del motor: el test parsea las respuestas del CLI.
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/External
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/tests
)
add_test(NAME cli-tests COMMAND cli-tests)
