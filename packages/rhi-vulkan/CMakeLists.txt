find_package(Vulkan REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(vke-rhi-vulkan STATIC
    src/vulkan_frame_loop.cpp
    src/vulkan_context.cpp
)

add_library(vke::rhi_vulkan ALIAS vke-rhi-vulkan)

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

vke_configure_target(vke-rhi-vulkan)
