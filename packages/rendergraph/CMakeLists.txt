add_library(vke-rendergraph INTERFACE)
add_library(vke::rendergraph ALIAS vke-rendergraph)

target_include_directories(vke-rendergraph
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-rendergraph
    INTERFACE
        vke::core
)

vke_configure_target(vke-rendergraph)
