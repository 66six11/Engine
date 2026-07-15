cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaHostRuntimeContract VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()

add_library(asharia-host-runtime-contract INTERFACE)
add_library(asharia::host_runtime_contract ALIAS asharia-host-runtime-contract)

target_include_directories(asharia-host-runtime-contract
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include-contract"
)

asharia_configure_target(asharia-host-runtime-contract)

add_library(asharia-host-runtime-registration STATIC
    src/static_factory_registration.cpp
    src/static_factory_registration_snapshot_json.cpp
    src/static_factory_registration_state.cpp
)
add_library(asharia::host_runtime_registration ALIAS
    asharia-host-runtime-registration)

target_include_directories(asharia-host-runtime-registration
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-host-runtime-registration
    PUBLIC
        asharia::host_runtime_contract
)

asharia_configure_target(asharia-host-runtime-registration)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-host-runtime-registration-tests
      tests/static_factory_registration_tests.cpp
  )

  target_link_libraries(asharia-host-runtime-registration-tests
      PRIVATE
          asharia::host_runtime_registration
  )

  asharia_configure_target(asharia-host-runtime-registration-tests)

  add_test(
      NAME asharia-host-runtime-registration-tests
      COMMAND asharia-host-runtime-registration-tests
  )

  add_executable(asharia-host-runtime-registration-snapshot-json-tests
      tests/static_factory_registration_snapshot_json_tests.cpp
  )

  target_link_libraries(asharia-host-runtime-registration-snapshot-json-tests
      PRIVATE
          asharia::host_runtime_registration
  )

  asharia_configure_target(asharia-host-runtime-registration-snapshot-json-tests)

  add_test(
      NAME asharia-host-runtime-registration-snapshot-json-tests
      COMMAND asharia-host-runtime-registration-snapshot-json-tests
  )
endif()
