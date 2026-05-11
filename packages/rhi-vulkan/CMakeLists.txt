cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEngineRhiVulkan VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()
vke_require_package_target(vke::core engine/core)
vke_require_package_target(vke::rendergraph packages/rendergraph)

find_package(Vulkan REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(vke-rhi-vulkan STATIC
    src/vulkan_frame_loop.cpp
    src/vulkan_context.cpp
    src/vulkan_pipeline.cpp
    src/vulkan_buffer.cpp
    src/vulkan_image.cpp
    src/deferred_deletion_queue.cpp
)

add_library(vke::rhi_vulkan ALIAS vke-rhi-vulkan)

add_library(vke-rhi-vulkan-rendergraph INTERFACE)
add_library(vke::rhi_vulkan_rendergraph ALIAS vke-rhi-vulkan-rendergraph)

target_include_directories(vke-rhi-vulkan
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${Vulkan_INCLUDE_DIRS}"
)

target_link_libraries(vke-rhi-vulkan
    PUBLIC
        vke::core
        Vulkan::Vulkan
        vulkan-headers::vulkan-headers
        GPUOpen::VulkanMemoryAllocator
)

target_link_libraries(vke-rhi-vulkan-rendergraph
    INTERFACE
        vke::rhi_vulkan
        vke::rendergraph
)

target_include_directories(vke-rhi-vulkan-rendergraph
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include-rendergraph"
)

vke_configure_target(vke-rhi-vulkan)
vke_configure_target(vke-rhi-vulkan-rendergraph)
