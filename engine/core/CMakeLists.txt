add_library(vke-core STATIC
    src/log.cpp
)

add_library(vke::core ALIAS vke-core)

target_include_directories(vke-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

vke_configure_target(vke-core)
