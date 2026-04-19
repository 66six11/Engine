cmake_minimum_required(VERSION 3.28)

project(
    VkEngine
    VERSION 0.1.0
    DESCRIPTION "Package-first Vulkan render graph engine"
    LANGUAGES CXX
)

option(VKE_BUILD_APPS "Build VkEngine host applications." ON)
option(VKE_BUILD_TESTS "Build VkEngine tests." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(VkeCompilerOptions)

add_subdirectory(engine/core)
add_subdirectory(engine/platform)

add_subdirectory(packages/window-glfw)
add_subdirectory(packages/rhi-vulkan)
add_subdirectory(packages/rendergraph)
add_subdirectory(packages/shader-slang)
add_subdirectory(packages/renderer-basic)

if(VKE_BUILD_APPS)
    add_subdirectory(apps/sample-viewer)
endif()

if(VKE_BUILD_TESTS)
    enable_testing()
endif()
