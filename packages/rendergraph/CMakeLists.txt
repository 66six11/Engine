cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineRenderGraph VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-rendergraph STATIC
    src/render_graph.cpp
)
add_library(asharia::rendergraph ALIAS asharia-rendergraph)

target_include_directories(asharia-rendergraph
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-rendergraph
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-rendergraph)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-rendergraph-compile-tests
      tests/rendergraph_compile_tests.cpp
  )

  target_link_libraries(asharia-rendergraph-compile-tests
      PRIVATE
          asharia::rendergraph
  )

  asharia_configure_target(asharia-rendergraph-compile-tests)

  add_test(NAME asharia-rendergraph-compile-tests COMMAND asharia-rendergraph-compile-tests)
endif()
