cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaProjectBootstrap VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::archive packages/archive)
asharia_require_package_target(asharia::project_core_io packages/project-core)
asharia_require_package_target(asharia::host_runtime_provider_bridge engine/host-runtime)

add_library(asharia-project-bootstrap-reader STATIC
    src/project_bootstrap_reader.cpp
    src/project_bootstrap_summary.cpp
)
add_library(asharia::project_bootstrap_reader ALIAS asharia-project-bootstrap-reader)

target_include_directories(asharia-project-bootstrap-reader
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-project-bootstrap-reader
    PUBLIC
        asharia::core
    PRIVATE
        asharia::archive
        asharia::project_core_io
)

asharia_configure_target(asharia-project-bootstrap-reader)

add_library(asharia-project-bootstrap-provider STATIC
    src/project_bootstrap_application.cpp
    src/project_bootstrap_provider.cpp
)
add_library(asharia::project_bootstrap_provider ALIAS asharia-project-bootstrap-provider)

target_include_directories(asharia-project-bootstrap-provider
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-project-bootstrap-provider
    PUBLIC
        asharia::host_runtime_contract
    PRIVATE
        asharia::host_runtime_provider_bridge
        asharia::project_bootstrap_reader
)

asharia_configure_target(asharia-project-bootstrap-provider)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-project-bootstrap-tests
      tests/project_bootstrap_tests.cpp
  )

  target_link_libraries(asharia-project-bootstrap-tests
      PRIVATE
          asharia::host_runtime_contract
          asharia::project_bootstrap_reader
          asharia::project_core_io
  )

  asharia_configure_target(asharia-project-bootstrap-tests)

  add_test(NAME asharia-project-bootstrap-tests COMMAND asharia-project-bootstrap-tests)
endif()
