add_library(vke-rhi-vulkan INTERFACE)
add_library(vke::rhi_vulkan ALIAS vke-rhi-vulkan)

target_include_directories(vke-rhi-vulkan
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-rhi-vulkan
    INTERFACE
        vke::core
)

vke_configure_target(vke-rhi-vulkan)
