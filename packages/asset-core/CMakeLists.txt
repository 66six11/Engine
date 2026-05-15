cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineAssetCore VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-asset-core STATIC
    src/asset_guid.cpp
    src/asset_metadata.cpp
    src/asset_reference.cpp
)

add_library(asharia::asset_core ALIAS asharia-asset-core)

target_include_directories(asharia-asset-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-asset-core
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-asset-core)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-asset-core-smoke-tests
      tests/asset_core_smoke_tests.cpp
  )

  target_link_libraries(asharia-asset-core-smoke-tests
      PRIVATE
          asharia::asset_core
  )

  asharia_configure_target(asharia-asset-core-smoke-tests)

  add_test(NAME asharia-asset-core-smoke-tests COMMAND asharia-asset-core-smoke-tests)
endif()
