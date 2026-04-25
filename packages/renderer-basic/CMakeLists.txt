add_library(vke-renderer-basic INTERFACE)
add_library(vke::renderer_basic ALIAS vke-renderer-basic)
add_library(vke-renderer-basic-vulkan INTERFACE)
add_library(vke::renderer_basic_vulkan ALIAS vke-renderer-basic-vulkan)

target_include_directories(vke-renderer-basic
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-renderer-basic
    INTERFACE
        vke::core
        vke::rendergraph
        vke::shader_slang
)

target_link_libraries(vke-renderer-basic-vulkan
    INTERFACE
        vke::renderer_basic
        vke::rhi_vulkan
        vke::rhi_vulkan_rendergraph
)

vke_configure_target(vke-renderer-basic)
vke_configure_target(vke-renderer-basic-vulkan)
