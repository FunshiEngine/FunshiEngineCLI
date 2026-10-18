cmake_minimum_required(VERSION 3.15)

project(FunshiEngineCLI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# No definir targets del motor aquí; usaremos solo lo necesario
option(FUNSHI_ENGINE_SUBMODULE "Usar submódulo extern/FunshiEngine" ON)

if(FUNSHI_ENGINE_SUBMODULE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/CMakeLists.txt")
        add_subdirectory(extern/FunshiEngine/FunshiEngineGL EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "Submódulo extern/FunshiEngine no encontrado")
    endif()
else()
    include(FetchContent)
    FetchContent_Declare(funshi_engine
        GIT_REPOSITORY https://github.com/FunshiEngine/FunshiEngineOpenGL.git
        GIT_TAG develop
    )
    FetchContent_MakeAvailable(funshi_engine)
endif()

add_executable(FunshiCLI
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_include_directories(FunshiCLI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/External
)

target_link_libraries(FunshiCLI PRIVATE
    funshi_engine
    funshi_project_core
    funshi_diagnostics_core
)

enable_testing()
add_executable(cli-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/CliTests.cpp
)
add_test(NAME cli-tests COMMAND cli-tests)
