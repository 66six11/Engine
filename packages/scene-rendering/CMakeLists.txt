cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineSceneRendering VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::asset_core packages/asset-core)
asharia_require_package_target(asharia::scene_core packages/scene-core)
asharia_require_package_target(asharia::renderer_basic packages/renderer-basic)

add_library(asharia-scene-rendering STATIC
    src/scene_mesh_extraction.cpp
)

add_library(asharia::scene_rendering ALIAS asharia-scene-rendering)

target_include_directories(asharia-scene-rendering
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-scene-rendering
    PUBLIC
        asharia::asset_core
        asharia::renderer_basic
        asharia::scene_core
)

asharia_configure_target(asharia-scene-rendering)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-scene-rendering-smoke-tests
      tests/scene_mesh_extraction_smoke_tests.cpp
  )

  target_link_libraries(asharia-scene-rendering-smoke-tests
      PRIVATE
          asharia::scene_rendering
  )

  asharia_configure_target(asharia-scene-rendering-smoke-tests)

  add_test(NAME asharia-scene-rendering-smoke-tests COMMAND asharia-scene-rendering-smoke-tests)
endif()
