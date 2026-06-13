cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineMaterialInstance VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::archive packages/archive)
asharia_require_package_target(asharia::asset_core packages/asset-core)
asharia_require_package_target(asharia::shader_authoring packages/shader-authoring)

add_library(asharia-material-instance STATIC
    src/amat_io.cpp
    src/amat_resolver.cpp
)

add_library(asharia::material_instance ALIAS asharia-material-instance)

target_include_directories(asharia-material-instance
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-material-instance
    PUBLIC
        asharia::core
        asharia::asset_core
        asharia::shader_authoring
    PRIVATE
        asharia::archive
)

asharia_configure_target(asharia-material-instance)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-material-instance-smoke-tests
      tests/material_instance_smoke_tests.cpp
  )

  target_link_libraries(asharia-material-instance-smoke-tests
      PRIVATE
          asharia::material_instance
  )

  asharia_configure_target(asharia-material-instance-smoke-tests)

  add_test(NAME asharia-material-instance-smoke-tests
           COMMAND asharia-material-instance-smoke-tests)
endif()
