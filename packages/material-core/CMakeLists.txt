cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineMaterialCore VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-material-core STATIC
    src/material_pipeline_key.cpp
    src/material_resource_signature.cpp
)

add_library(asharia::material_core ALIAS asharia-material-core)

target_include_directories(asharia-material-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-material-core
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-material-core)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-material-core-smoke-tests
      tests/material_core_smoke_tests.cpp
  )

  target_link_libraries(asharia-material-core-smoke-tests
      PRIVATE
          asharia::material_core
  )

  asharia_configure_target(asharia-material-core-smoke-tests)

  add_test(NAME asharia-material-core-smoke-tests COMMAND asharia-material-core-smoke-tests)
endif()
