cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEnginePersistence VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::schema packages/schema)
asharia_require_package_target(asharia::archive packages/archive)
asharia_require_package_target(asharia::cpp_binding packages/cpp-binding)

add_library(asharia-persistence STATIC
    src/migration.cpp
    src/persistence.cpp
)

add_library(asharia::persistence ALIAS asharia-persistence)

target_include_directories(asharia-persistence
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-persistence
    PUBLIC
        asharia::core
        asharia::schema
        asharia::archive
        asharia::cpp_binding
)

asharia_configure_target(asharia-persistence)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-persistence-smoke-tests
      tests/persistence_smoke_tests.cpp
  )

  target_link_libraries(asharia-persistence-smoke-tests
      PRIVATE
          asharia::persistence
  )

  asharia_configure_target(asharia-persistence-smoke-tests)

  add_test(NAME asharia-persistence-smoke-tests COMMAND asharia-persistence-smoke-tests)
endif()
