add_library(vke-platform INTERFACE)
add_library(vke::platform ALIAS vke-platform)

target_include_directories(vke-platform
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-platform
    INTERFACE
        vke::core
)

vke_configure_target(vke-platform)
