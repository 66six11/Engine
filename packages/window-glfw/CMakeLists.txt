add_library(vke-window-glfw INTERFACE)
add_library(vke::window_glfw ALIAS vke-window-glfw)

target_include_directories(vke-window-glfw
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-window-glfw
    INTERFACE
        vke::core
        vke::platform
)

vke_configure_target(vke-window-glfw)
