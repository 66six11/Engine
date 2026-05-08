add_library(vke-profiling INTERFACE)
add_library(vke::profiling ALIAS vke-profiling)

target_include_directories(vke-profiling
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

vke_configure_target(vke-profiling)
