cmake_minimum_required(VERSION 3.28)

project(
    AshariaEngine
    VERSION 0.1.0
    DESCRIPTION "Package-first Vulkan render graph engine"
    LANGUAGES CXX
)

option(ASHARIA_BUILD_APPS "Build Asharia Engine host applications." ON)
option(ASHARIA_BUILD_TESTS "Build Asharia Engine tests." OFF)
option(ASHARIA_ENABLE_CLANG_TIDY "Enable clang-tidy for supported builds and Visual Studio presets." OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(AshariaCompilerOptions)

if(ASHARIA_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(engine/core)
add_subdirectory(engine/platform)

add_subdirectory(packages/window-glfw)
add_subdirectory(packages/profiling)
add_subdirectory(packages/rendergraph)
add_subdirectory(packages/reflection)
add_subdirectory(packages/serialization)
add_subdirectory(packages/asset-core)
add_subdirectory(packages/rhi-vulkan)
add_subdirectory(packages/shader-slang)
add_subdirectory(packages/renderer-basic)

if(ASHARIA_BUILD_APPS)
    add_subdirectory(apps/editor)
    add_subdirectory(apps/sample-viewer)
endif()
