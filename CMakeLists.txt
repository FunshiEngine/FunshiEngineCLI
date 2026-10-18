cmake_minimum_required(VERSION 3.15)

project(FunshiEngineCLI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_ENGINE OFF CACHE BOOL "" FORCE)
set(BUILD_FUNSHI_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_RUNTIME OFF CACHE BOOL "" FORCE)

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

# Añadir include de Bullet si existe
find_package(Bullet QUIET)
if(Bullet_FOUND)
    include_directories(${BULLET_INCLUDE_DIRS})
elseif(EXISTS "/mingw64/include/bullet")
    include_directories("/mingw64/include/bullet")
elseif(EXISTS "C:/msys64/mingw64/include/bullet")
    include_directories("C:/msys64/mingw64/include/bullet")
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
