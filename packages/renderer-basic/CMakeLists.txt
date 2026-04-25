add_library(vke-renderer-basic INTERFACE)
add_library(vke::renderer_basic ALIAS vke-renderer-basic)

target_include_directories(vke-renderer-basic
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-renderer-basic
    INTERFACE
        vke::core
        vke::rhi_vulkan
        vke::rhi_vulkan_rendergraph
        vke::rendergraph
        vke::shader_slang
)

vke_configure_target(vke-renderer-basic)
