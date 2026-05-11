cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineReflection VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-reflection STATIC
    src/type_registry.cpp
)

add_library(asharia::reflection ALIAS asharia-reflection)

target_include_directories(asharia-reflection
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-reflection
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-reflection)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-reflection-smoke-tests
      tests/reflection_smoke_tests.cpp
  )

  target_link_libraries(asharia-reflection-smoke-tests
      PRIVATE
          asharia::reflection
  )

  asharia_configure_target(asharia-reflection-smoke-tests)

  add_test(NAME asharia-reflection-smoke-tests COMMAND asharia-reflection-smoke-tests)
endif()
