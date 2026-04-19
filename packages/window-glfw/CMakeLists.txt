find_package(glfw3 CONFIG REQUIRED)

add_library(vke-window-glfw STATIC
    src/glfw_window.cpp
)

add_library(vke::window_glfw ALIAS vke-window-glfw)

target_include_directories(vke-window-glfw
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-window-glfw
    PUBLIC
        vke::core
        vke::platform
        glfw
)

target_compile_definitions(vke-window-glfw PUBLIC GLFW_INCLUDE_NONE)

vke_configure_target(vke-window-glfw)
