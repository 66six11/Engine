cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineProjectCore VERSION 0.1.0 LANGUAGES C CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::archive packages/archive)

add_library(asharia-project-core STATIC
    src/project_descriptor.cpp
)

add_library(asharia::project_core ALIAS asharia-project-core)

add_library(asharia-project-core-io STATIC
    src/project_descriptor_io.cpp
)

add_library(asharia::project_core_io ALIAS asharia-project-core-io)

add_library(asharia-project-native SHARED
    src/project_native_api.cpp
)

add_library(asharia::project_native ALIAS asharia-project-native)

set_target_properties(asharia-project-native
    PROPERTIES
        OUTPUT_NAME asharia_project_native
)

target_include_directories(asharia-project-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-project-core
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-project-core)

target_include_directories(asharia-project-core-io
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-project-core-io
    PUBLIC
        asharia::project_core
    PRIVATE
        asharia::archive
)

asharia_configure_target(asharia-project-core-io)

target_include_directories(asharia-project-native
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-project-native
    PRIVATE
        asharia::project_core_io
)

target_compile_definitions(asharia-project-native
    PRIVATE
        ASHARIA_PROJECT_NATIVE_BUILD=1
)

asharia_configure_target(asharia-project-native)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  if(NOT CMAKE_C_COMPILER_LOADED)
    enable_language(C)
  endif()

  add_executable(asharia-project-core-smoke-tests
      tests/project_core_smoke_tests.cpp
  )

  target_link_libraries(asharia-project-core-smoke-tests
      PRIVATE
          asharia::project_core_io
  )

  asharia_configure_target(asharia-project-core-smoke-tests)

  add_test(NAME asharia-project-core-smoke-tests COMMAND asharia-project-core-smoke-tests)

  add_executable(asharia-project-native-smoke-tests
      tests/project_native_smoke_tests.cpp
  )

  target_link_libraries(asharia-project-native-smoke-tests
      PRIVATE
          asharia::project_native
          asharia::project_core_io
  )

  asharia_configure_target(asharia-project-native-smoke-tests)

  add_test(NAME asharia-project-native-smoke-tests COMMAND asharia-project-native-smoke-tests)

  add_executable(asharia-project-native-c-header-smoke
      tests/project_native_c_header_smoke.c
  )

  target_include_directories(asharia-project-native-c-header-smoke
      PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )

  add_test(
      NAME asharia-project-native-c-header-smoke
      COMMAND asharia-project-native-c-header-smoke
  )
endif()
