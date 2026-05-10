find_package(nlohmann_json CONFIG REQUIRED)

add_library(vke-serialization STATIC
    src/archive_value.cpp
    src/serializer.cpp
    src/text_archive.cpp
)

add_library(vke::serialization ALIAS vke-serialization)

target_include_directories(vke-serialization
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-serialization
    PUBLIC
        vke::core
        vke::reflection
    PRIVATE
        nlohmann_json::nlohmann_json
)

vke_configure_target(vke-serialization)
