add_executable(vke-sample-viewer
    src/main.cpp
)

add_executable(vke::sample_viewer ALIAS vke-sample-viewer)

add_dependencies(vke-sample-viewer vke-renderer-basic-shaders)

target_link_libraries(vke-sample-viewer
    PRIVATE
        vke::core
        vke::rendergraph
        vke::rhi_vulkan
        vke::rhi_vulkan_rendergraph
        vke::window_glfw
        vke::renderer_basic
        vke::renderer_basic_vulkan
)

vke_configure_target(vke-sample-viewer)
