cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineSchema VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-schema STATIC
    src/schema_registry.cpp
)

add_library(asharia::schema ALIAS asharia-schema)

target_include_directories(asharia-schema
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-schema
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-schema)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-schema-smoke-tests
      tests/schema_smoke_tests.cpp
  )

  target_link_libraries(asharia-schema-smoke-tests
      PRIVATE
          asharia::schema
  )

  asharia_configure_target(asharia-schema-smoke-tests)

  add_test(NAME asharia-schema-smoke-tests COMMAND asharia-schema-smoke-tests)
endif()
