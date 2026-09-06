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

asharia_require_package_target(asharia::asset_product_reader packages/asset-pipeline)
asharia_require_package_target(asharia::shader_slang packages/shader-slang)

add_library(asharia-resource-runtime STATIC
    src/mesh_resource_store.cpp
    src/shader_resource.cpp
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
        asharia::shader_slang
    PRIVATE
        asharia::asset_product_reader
)

asharia_configure_target(asharia-resource-runtime)

if(ASHARIA_BUILD_TESTS)
  enable_testing()
  asharia_require_package_target(asharia::editor_content packages/editor-content)
  add_executable(asharia-shader-resource-tests tests/shader_resource_tests.cpp)
  target_link_libraries(asharia-shader-resource-tests PRIVATE
      asharia::resource_runtime asharia::asset_pipeline asharia::editor_content asharia::asset_core_io)
  target_compile_definitions(asharia-shader-resource-tests PRIVATE
      ASHARIA_SHADER_RESOURCE_TEST_ROOT="${CMAKE_CURRENT_BINARY_DIR}/shader-resource-fixture")
  asharia_configure_target(asharia-shader-resource-tests)
  add_test(NAME asharia-shader-resource-tests COMMAND asharia-shader-resource-tests)


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
