cmake_minimum_required(VERSION 3.28)

project(
    AshariaEngine
    VERSION 0.1.0
    DESCRIPTION "Package-first Vulkan render graph engine"
    LANGUAGES CXX
)

option(ASHARIA_BUILD_APPS "Build Asharia Engine host applications." ON)
option(ASHARIA_BUILD_TESTS "Build Asharia Engine tests." OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(AshariaCompilerOptions)
include(AshariaGeneratedHost)

if(ASHARIA_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(engine/core)
add_subdirectory(engine/platform)
add_subdirectory(engine/host-runtime)

add_subdirectory(packages/window-glfw)
add_subdirectory(packages/profiling)
add_subdirectory(packages/rendergraph)
add_subdirectory(packages/schema)
add_subdirectory(packages/archive)
add_subdirectory(packages/cpp-binding)
add_subdirectory(packages/persistence)
add_subdirectory(packages/asset-core)
add_subdirectory(packages/scene-core)
add_subdirectory(packages/reflection)
add_subdirectory(packages/serialization)
add_subdirectory(packages/project-core)
add_subdirectory(packages/project-bootstrap)
add_subdirectory(packages/resource-runtime)
add_subdirectory(packages/material-core)
add_subdirectory(packages/rhi-vulkan)
add_subdirectory(packages/shader-slang)
add_subdirectory(packages/shader-authoring)
add_subdirectory(packages/material-instance)
add_subdirectory(packages/asset-pipeline)
add_subdirectory(packages/editor-content)
add_subdirectory(packages/shader-material-adapter)
add_subdirectory(packages/renderer-basic)
add_subdirectory(packages/scene-rendering)

asharia_include_generated_host_template()

add_subdirectory(tools/asset-processor)

if(ASHARIA_BUILD_APPS)
    add_subdirectory(apps/editor)
    add_subdirectory(apps/sample-viewer)
endif()

add_custom_target(asharia-tidy
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_clang_tidy.py"
        --build-dir
        "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)
