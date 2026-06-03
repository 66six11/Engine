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
    src/render_graph_builder.cpp
    src/render_graph_command_schema_validation.cpp
    src/render_graph_command_list.cpp
    src/render_graph_compile.cpp
    src/render_graph_compiled_pass.cpp
    src/render_graph_buffer_access_validation.cpp
    src/render_graph_buffer_dependency_builder.cpp
    src/render_graph_declaration_view.cpp
    src/render_graph_dependency_builder.cpp
    src/render_graph_dependency_culling.cpp
    src/render_graph_dependency_sort.cpp
    src/render_graph_debug_detail_tables.cpp
    src/render_graph_debug_names.cpp
    src/render_graph_debug_summary_tables.cpp
    src/render_graph_debug_tables.cpp
    src/render_graph_diagnostics.cpp
    src/render_graph_diagnostics_pass_snapshot.cpp
    src/render_graph_diagnostics_snapshot.cpp
    src/render_graph_execution.cpp
    src/render_graph_image_access_validation.cpp
    src/render_graph_image_dependency_builder.cpp
    src/render_graph_lifetime.cpp
    src/render_graph_pass_declarations.cpp
    src/render_graph_pass_queries.cpp
    src/render_graph_registry.cpp
    src/render_graph_resource_declarations.cpp
    src/render_graph_resource_schema_validation.cpp
    src/render_graph_buffer_slot_validation.cpp
    src/render_graph_image_slot_validation.cpp
    src/render_graph_schema_queries.cpp
    src/render_graph_schema_validation.cpp
    src/render_graph_slot_validation.cpp
    src/render_graph_validation.cpp
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

  add_executable(asharia-rendergraph-header-tests
      tests/rendergraph_header_tests.cpp
      tests/rendergraph_command_list_header_test.cpp
      tests/rendergraph_types_header_test.cpp
      tests/rendergraph_pass_context_header_test.cpp
      tests/rendergraph_execution_header_test.cpp
      tests/rendergraph_builder_header_test.cpp
      tests/rendergraph_compile_header_test.cpp
      tests/rendergraph_diagnostics_header_test.cpp
      tests/rendergraph_aggregate_header_test.cpp
  )

  target_link_libraries(asharia-rendergraph-compile-tests
      PRIVATE
          asharia::rendergraph
  )

  target_link_libraries(asharia-rendergraph-header-tests
      PRIVATE
          asharia::rendergraph
  )

  asharia_configure_target(asharia-rendergraph-compile-tests)
  asharia_configure_target(asharia-rendergraph-header-tests)

  add_test(NAME asharia-rendergraph-compile-tests COMMAND asharia-rendergraph-compile-tests)
  add_test(NAME asharia-rendergraph-header-tests COMMAND asharia-rendergraph-header-tests)
endif()
