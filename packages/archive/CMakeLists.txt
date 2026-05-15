cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineArchive VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(asharia-archive STATIC
    src/archive_value.cpp
    src/json_archive.cpp
)

add_library(asharia::archive ALIAS asharia-archive)

target_include_directories(asharia-archive
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-archive
    PUBLIC
        asharia::core
    PRIVATE
        nlohmann_json::nlohmann_json
)

asharia_configure_target(asharia-archive)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-archive-smoke-tests
      tests/archive_smoke_tests.cpp
  )

  target_link_libraries(asharia-archive-smoke-tests
      PRIVATE
          asharia::archive
  )

  asharia_configure_target(asharia-archive-smoke-tests)

  add_test(NAME asharia-archive-smoke-tests COMMAND asharia-archive-smoke-tests)
endif()
