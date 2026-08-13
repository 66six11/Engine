cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineMeshProduct VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::asset_core packages/asset-core)

add_library(asharia-mesh-product STATIC
    src/mesh_product_internal.cpp
    src/mesh_product_v1.cpp
)

add_library(asharia::mesh_product ALIAS asharia-mesh-product)

target_include_directories(asharia-mesh-product
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-mesh-product
    PUBLIC
        asharia::asset_core
        asharia::core
)

asharia_configure_target(asharia-mesh-product)

add_library(asharia-mesh-product-writer STATIC
    src/mesh_product_writer_v1.cpp
)

add_library(asharia::mesh_product_writer ALIAS asharia-mesh-product-writer)

target_include_directories(asharia-mesh-product-writer
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-mesh-product-writer
    PUBLIC
        asharia::mesh_product
)

asharia_configure_target(asharia-mesh-product-writer)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-mesh-product-tests
      tests/mesh_product_tests.cpp
  )

  target_link_libraries(asharia-mesh-product-tests
      PRIVATE
          asharia::mesh_product_writer
  )

  asharia_configure_target(asharia-mesh-product-tests)

  add_test(NAME asharia-mesh-product-tests COMMAND asharia-mesh-product-tests)
endif()
