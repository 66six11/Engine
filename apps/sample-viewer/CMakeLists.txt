add_library(vke-sample-viewer-host INTERFACE)
add_library(vke::sample_viewer_host ALIAS vke-sample-viewer-host)

target_link_libraries(vke-sample-viewer-host
    INTERFACE
        vke::core
        vke::window_glfw
        vke::renderer_basic
)

vke_configure_target(vke-sample-viewer-host)
