cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineAssetProcessor VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::asset_core_io packages/asset-core)
asharia_require_package_target(asharia::asset_pipeline packages/asset-pipeline)
asharia_require_package_target(asharia::project_core_io packages/project-core)

add_executable(asharia-asset-processor
    src/asset_processor_dry_run.cpp
    src/asset_processor_execute.cpp
    src/asset_processor_project_input.cpp
    src/asset_processor_smoke.cpp
    src/asset_processor_text.cpp
    src/main.cpp
)

target_link_libraries(asharia-asset-processor
    PRIVATE
        asharia::core
        asharia::asset_core_io
        asharia::asset_pipeline
        asharia::project_core_io
)

asharia_configure_target(asharia-asset-processor)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_test(NAME asharia-asset-processor-smoke-dry-run
      COMMAND asharia-asset-processor --smoke-dry-run
  )

  add_test(NAME asharia-asset-processor-smoke-product-execution
      COMMAND asharia-asset-processor --smoke-product-execution
  )
endif()
