cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineSceneCore VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-scene-core STATIC
    src/world.cpp
)

add_library(asharia::scene_core ALIAS asharia-scene-core)

target_include_directories(asharia-scene-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-scene-core
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-scene-core)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-scene-core-smoke-tests
      tests/scene_core_smoke_tests.cpp
  )

  target_link_libraries(asharia-scene-core-smoke-tests
      PRIVATE
          asharia::scene_core
  )

  asharia_configure_target(asharia-scene-core-smoke-tests)

  add_test(NAME asharia-scene-core-smoke-tests COMMAND asharia-scene-core-smoke-tests)
endif()
