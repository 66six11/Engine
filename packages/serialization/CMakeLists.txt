cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineSerialization VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::reflection packages/reflection)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(asharia-serialization STATIC
    src/archive_value.cpp
    src/migration.cpp
    src/serializer.cpp
    src/text_archive.cpp
)

add_library(asharia::serialization ALIAS asharia-serialization)

target_include_directories(asharia-serialization
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-serialization
    PUBLIC
        asharia::core
        asharia::reflection
    PRIVATE
        nlohmann_json::nlohmann_json
)

asharia_configure_target(asharia-serialization)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-serialization-smoke-tests
      tests/serialization_smoke_tests.cpp
  )

  target_link_libraries(asharia-serialization-smoke-tests
      PRIVATE
          asharia::serialization
  )

  asharia_configure_target(asharia-serialization-smoke-tests)

  add_test(NAME asharia-serialization-smoke-tests COMMAND asharia-serialization-smoke-tests)
endif()
