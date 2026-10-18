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

find_package(Bullet QUIET)
if(Bullet_FOUND)
    include_directories(${BULLET_INCLUDE_DIRS})
elseif(EXISTS "/mingw64/include/bullet")
    include_directories("/mingw64/include/bullet")
elseif(EXISTS "C:/msys64/mingw64/include/bullet")
    include_directories("C:/msys64/mingw64/include/bullet")
endif()

find_package(OpenGL REQUIRED)
find_package(GLFW3 QUIET)

add_executable(FunshiCLI
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_include_directories(FunshiCLI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL/External
)

target_compile_definitions(FunshiCLI PRIVATE
    FUNSHI_VERSION="0.5.4"
)

target_link_libraries(FunshiCLI PRIVATE
    funshi_project_core
    funshi_diagnostics_core
)

find_library(FUNSHI_ENGINE_LIB NAMES funshi_engine
    PATHS
    ${CMAKE_BINARY_DIR}/extern/FunshiEngine/FunshiEngineGL
    ${CMAKE_BINARY_DIR}/lib
    C:/Users/gianf/Desktop/Projects/FunshiEngineOpenGL/FunshiEngineGL/build
    NO_DEFAULT_PATH)
if(FUNSHI_ENGINE_LIB)
    target_link_libraries(FunshiCLI PRIVATE ${FUNSHI_ENGINE_LIB})
elseif(TARGET funshi_engine)
    target_link_libraries(FunshiCLI PRIVATE funshi_engine)
endif()

if(GLFW3_FOUND)
    target_link_libraries(FunshiCLI PRIVATE glfw)
endif()
target_link_libraries(FunshiCLI PRIVATE OpenGL::GL)

enable_testing()
add_executable(cli-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/CliTests.cpp
)
target_include_directories(cli-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/FunshiEngine/FunshiEngineGL
)
add_test(NAME cli-tests COMMAND cli-tests)
