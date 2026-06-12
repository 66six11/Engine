cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineAssetPipeline VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
find_package(stb CONFIG REQUIRED)
asharia_require_package_target(asharia::archive packages/archive)
asharia_require_package_target(asharia::asset_core packages/asset-core)
asharia_require_package_target(asharia::asset_core_io packages/asset-core)

add_library(asharia-asset-pipeline STATIC
    src/asset_import_planning.cpp
    src/asset_product_blob.cpp
    src/asset_product_execution.cpp
    src/asset_product_manifest_io.cpp
    src/asset_scanned_import_planning.cpp
    src/asset_source_scan.cpp
    src/asset_source_discovery.cpp
    src/asset_source_snapshot.cpp
    src/asset_texture_import.cpp
    src/asset_texture_import_profile.cpp
)

add_library(asharia::asset_pipeline ALIAS asharia-asset-pipeline)

target_include_directories(asharia-asset-pipeline
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-asset-pipeline
    PUBLIC
        asharia::asset_core
    PRIVATE
        asharia::archive
        asharia::asset_core_io
        stb::stb
)

asharia_configure_target(asharia-asset-pipeline)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-asset-pipeline-smoke-tests
      tests/asset_pipeline_smoke_tests.cpp
  )

  add_executable(asharia-asset-pipeline-header-tests
      tests/asset_pipeline_header_tests.cpp
      tests/asset_import_planning_header_test.cpp
      tests/asset_product_blob_header_test.cpp
      tests/asset_product_execution_header_test.cpp
      tests/asset_product_manifest_io_header_test.cpp
      tests/asset_scanned_import_planning_header_test.cpp
      tests/asset_source_scan_header_test.cpp
      tests/asset_source_discovery_header_test.cpp
      tests/asset_source_snapshot_header_test.cpp
      tests/asset_texture_import_header_test.cpp
      tests/asset_texture_import_profile_header_test.cpp
  )

  target_link_libraries(asharia-asset-pipeline-smoke-tests
      PRIVATE
          asharia::asset_pipeline
          asharia::asset_core_io
  )

  target_link_libraries(asharia-asset-pipeline-header-tests
      PRIVATE
          asharia::asset_pipeline
  )

  asharia_configure_target(asharia-asset-pipeline-smoke-tests)
  asharia_configure_target(asharia-asset-pipeline-header-tests)

  add_test(NAME asharia-asset-pipeline-smoke-tests COMMAND asharia-asset-pipeline-smoke-tests)
  add_test(NAME asharia-asset-pipeline-header-tests COMMAND asharia-asset-pipeline-header-tests)
endif()
