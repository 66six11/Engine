cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineResourceRuntime VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::asset_artifact packages/asset-artifact)
asharia_require_package_target(asharia::asset_core packages/asset-core)
asharia_require_package_target(asharia::mesh_product packages/mesh-product)

add_library(asharia-resource-runtime STATIC
    src/mesh_resource_store.cpp
)

add_library(asharia::resource_runtime ALIAS asharia-resource-runtime)

target_include_directories(asharia-resource-runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-resource-runtime
    PUBLIC
        asharia::asset_artifact
        asharia::asset_core
        asharia::core
        asharia::mesh_product
)

asharia_configure_target(asharia-resource-runtime)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-resource-runtime-smoke-tests
      tests/resource_runtime_smoke_tests.cpp
  )

  target_link_libraries(asharia-resource-runtime-smoke-tests
      PRIVATE
          asharia::resource_runtime
          asharia::mesh_product_writer
  )

  asharia_configure_target(asharia-resource-runtime-smoke-tests)

  add_test(NAME asharia-resource-runtime-smoke-tests COMMAND asharia-resource-runtime-smoke-tests)
endif()
