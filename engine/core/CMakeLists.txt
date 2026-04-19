add_library(vke-core INTERFACE)
add_library(vke::core ALIAS vke-core)

target_include_directories(vke-core
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

vke_configure_target(vke-core)
