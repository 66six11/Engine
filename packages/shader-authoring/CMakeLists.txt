cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineShaderAuthoring VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-shader-authoring STATIC
    src/ashader_generated_slang.cpp
    src/ashader_parser.cpp
)
add_library(asharia::shader_authoring ALIAS asharia-shader-authoring)

target_include_directories(asharia-shader-authoring
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-shader-authoring
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-shader-authoring)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-shader-authoring-smoke-tests
      tests/ashader_parser_smoke_tests.cpp
  )

  target_link_libraries(asharia-shader-authoring-smoke-tests
      PRIVATE
          asharia::shader_authoring
  )

  asharia_configure_target(asharia-shader-authoring-smoke-tests)

  add_test(NAME asharia-shader-authoring-smoke-tests
           COMMAND asharia-shader-authoring-smoke-tests)
endif()
