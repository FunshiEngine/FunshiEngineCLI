cmake_minimum_required(VERSION 3.15)

project(FunshiEngineCLI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opción: usar submódulo o FetchContent para enlazar FunshiEngine
option(FUNSHI_ENGINE_SUBMODULE "Usar submódulo extern/FunshiEngine" ON)

if(FUNSHI_ENGINE_SUBMODULE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/CMakeLists.txt")
        add_subdirectory(extern/FunshiEngine/FunshiEngineGL)
    else()
        message(FATAL_ERROR "Submódulo extern/FunshiEngine no encontrado. Ejecuta: git submodule add https://github.com/FunshiEngine/FunshiEngineOpenGL.git extern/FunshiEngine")
    endif()
else()
    include(FetchContent)
    FetchContent_Declare(funshi_engine
        GIT_REPOSITORY https://github.com/FunshiEngine/FunshiEngineOpenGL.git
        GIT_TAG develop
    )
    FetchContent_MakeAvailable(funshi_engine)
endif()

# Binario FunshiCLI
add_executable(FunshiCLI
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_include_directories(FunshiCLI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/External
)

target_compile_definitions(FunshiCLI PRIVATE
    FUNSHI_VERSION="${FUNSHI_ENGINE_VERSION}")
# Obtener versión del engine si está definida
get_target_property(_funshi_ver funshi_engine VERSION 2>/dev/null)
if(_funshi_ver)
    set_target_properties(FunshiCLI PROPERTIES
        COMPILE_DEFINITIONS "FUNSHI_VERSION=\"${_funshi_ver}\"")
endif()

target_link_libraries(FunshiCLI PRIVATE
    funshi_engine
    funshi_project_core
    funshi_diagnostics_core
)

# Tests CLI
enable_testing()
add_executable(cli-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/CliTests.cpp
)
set_target_properties(cli-tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
add_test(NAME cli-tests COMMAND cli-tests)
