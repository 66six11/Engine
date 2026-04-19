add_library(vke-shader-slang INTERFACE)
add_library(vke::shader_slang ALIAS vke-shader-slang)

target_include_directories(vke-shader-slang
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-shader-slang
    INTERFACE
        vke::core
)

vke_configure_target(vke-shader-slang)
