cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineCore VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()

add_library(asharia-core STATIC
    src/file_io.cpp
    src/log.cpp
)

add_library(asharia::core ALIAS asharia-core)

target_include_directories(asharia-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

asharia_configure_target(asharia-core)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-core-file-io-tests
      tests/file_io_tests.cpp
  )

  target_include_directories(asharia-core-file-io-tests
      PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )

  target_link_libraries(asharia-core-file-io-tests
      PRIVATE
          asharia::core
  )

  asharia_configure_target(asharia-core-file-io-tests)

  add_test(NAME asharia-core-file-io-tests COMMAND asharia-core-file-io-tests)
endif()
