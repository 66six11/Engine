add_library(vke-reflection STATIC
    src/type_registry.cpp
)

add_library(vke::reflection ALIAS vke-reflection)

target_include_directories(vke-reflection
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-reflection
    PUBLIC
        vke::core
)

vke_configure_target(vke-reflection)
