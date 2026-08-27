cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineAssetArtifact VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::asset_core packages/asset-core)

add_library(asharia-asset-artifact STATIC
    src/asset_artifact_v1.cpp
)

add_library(asharia::asset_artifact ALIAS asharia-asset-artifact)

target_include_directories(asharia-asset-artifact
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-asset-artifact
    PUBLIC
        asharia::asset_core
        asharia::core
)

asharia_configure_target(asharia-asset-artifact)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-asset-artifact-tests
      tests/asset_artifact_tests.cpp
  )

  target_link_libraries(asharia-asset-artifact-tests
      PRIVATE
          asharia::asset_artifact
  )

  asharia_configure_target(asharia-asset-artifact-tests)

  add_test(NAME asharia-asset-artifact-tests COMMAND asharia-asset-artifact-tests)
endif()
