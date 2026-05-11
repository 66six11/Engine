cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineRhiVulkan VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::rendergraph packages/rendergraph)

find_package(Vulkan REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(asharia-rhi-vulkan STATIC
    src/vulkan_frame_loop.cpp
    src/vulkan_context.cpp
    src/vulkan_pipeline.cpp
    src/vulkan_buffer.cpp
    src/vulkan_image.cpp
    src/deferred_deletion_queue.cpp
)

add_library(asharia::rhi_vulkan ALIAS asharia-rhi-vulkan)

add_library(asharia-rhi-vulkan-rendergraph INTERFACE)
add_library(asharia::rhi_vulkan_rendergraph ALIAS asharia-rhi-vulkan-rendergraph)

target_include_directories(asharia-rhi-vulkan
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${Vulkan_INCLUDE_DIRS}"
)

target_link_libraries(asharia-rhi-vulkan
    PUBLIC
        asharia::core
        Vulkan::Vulkan
        vulkan-headers::vulkan-headers
        GPUOpen::VulkanMemoryAllocator
)

target_link_libraries(asharia-rhi-vulkan-rendergraph
    INTERFACE
        asharia::rhi_vulkan
        asharia::rendergraph
)

target_include_directories(asharia-rhi-vulkan-rendergraph
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include-rendergraph"
)

asharia_configure_target(asharia-rhi-vulkan)
asharia_configure_target(asharia-rhi-vulkan-rendergraph)
