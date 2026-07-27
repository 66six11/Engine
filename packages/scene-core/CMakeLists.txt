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

set_target_properties(asharia-scene-core
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

target_include_directories(asharia-scene-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-scene-core
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-scene-core)

add_library(asharia-scene-native SHARED
    src/world_native_api.cpp
)

add_library(asharia::scene_native ALIAS asharia-scene-native)

set_target_properties(asharia-scene-native
    PROPERTIES
        OUTPUT_NAME asharia_scene_native
)

target_include_directories(asharia-scene-native
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-scene-native
    PRIVATE
        asharia::scene_core
)

target_compile_definitions(asharia-scene-native
    PRIVATE
        ASHARIA_SCENE_NATIVE_BUILD=1
)

asharia_configure_target(asharia-scene-native)

if(ASHARIA_BUILD_TESTS)
  enable_testing()
  enable_language(C)

  add_executable(asharia-scene-core-smoke-tests
      tests/scene_core_smoke_tests.cpp
  )

  target_link_libraries(asharia-scene-core-smoke-tests
      PRIVATE
          asharia::scene_core
  )

  asharia_configure_target(asharia-scene-core-smoke-tests)

  add_test(NAME asharia-scene-core-smoke-tests COMMAND asharia-scene-core-smoke-tests)

  add_executable(asharia-scene-native-lifecycle-smoke-tests
      tests/scene_native_lifecycle_smoke_tests.cpp
  )

  target_link_libraries(asharia-scene-native-lifecycle-smoke-tests
      PRIVATE
          asharia::scene_native
  )

  asharia_configure_target(asharia-scene-native-lifecycle-smoke-tests)

  add_test(
      NAME asharia-scene-native-lifecycle-smoke-tests
      COMMAND asharia-scene-native-lifecycle-smoke-tests
  )

  add_executable(asharia-scene-native-c-smoke-tests
      tests/scene_native_c_smoke_tests.c
  )

  target_link_libraries(asharia-scene-native-c-smoke-tests
      PRIVATE
          asharia::scene_native
  )

  set_target_properties(asharia-scene-native-c-smoke-tests
      PROPERTIES
          C_STANDARD 11
          C_STANDARD_REQUIRED ON
          C_EXTENSIONS OFF
  )

  if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(asharia-scene-native-c-smoke-tests PRIVATE /W4)
  else()
    target_compile_options(
        asharia-scene-native-c-smoke-tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
  endif()

  add_test(NAME asharia-scene-native-c-smoke-tests COMMAND asharia-scene-native-c-smoke-tests)
endif()
