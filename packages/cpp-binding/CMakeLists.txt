cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineCppBinding VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::schema packages/schema)

add_library(asharia-cpp-binding STATIC
    src/binding_registry.cpp
)

add_library(asharia::cpp_binding ALIAS asharia-cpp-binding)

target_include_directories(asharia-cpp-binding
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-cpp-binding
    PUBLIC
        asharia::core
        asharia::schema
)

asharia_configure_target(asharia-cpp-binding)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-cpp-binding-smoke-tests
      tests/cpp_binding_smoke_tests.cpp
  )

  target_link_libraries(asharia-cpp-binding-smoke-tests
      PRIVATE
          asharia::cpp_binding
  )

  asharia_configure_target(asharia-cpp-binding-smoke-tests)

  add_test(NAME asharia-cpp-binding-smoke-tests COMMAND asharia-cpp-binding-smoke-tests)
endif()
