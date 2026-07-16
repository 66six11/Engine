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

add_library(asharia-host-runtime-provider-bridge INTERFACE)
add_library(asharia::host_runtime_provider_bridge ALIAS
    asharia-host-runtime-provider-bridge)

target_include_directories(asharia-host-runtime-provider-bridge
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include-provider"
)

target_link_libraries(asharia-host-runtime-provider-bridge
    INTERFACE
        asharia::host_runtime_contract
)

asharia_configure_target(asharia-host-runtime-provider-bridge)

add_library(asharia-host-runtime-registration STATIC
    src/static_factory_callback_table.cpp
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

add_library(asharia-host-runtime-activation-eligibility STATIC
    src/activation_eligibility.cpp
    src/admitted_static_factory_callback_table_access.cpp
    src/admitted_static_factory_recording.cpp
)
add_library(asharia::host_runtime_activation_eligibility ALIAS
    asharia-host-runtime-activation-eligibility)

target_include_directories(asharia-host-runtime-activation-eligibility
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-host-runtime-activation-eligibility
    PUBLIC
        asharia::host_runtime_registration
)

asharia_configure_target(asharia-host-runtime-activation-eligibility)

add_library(asharia-host-runtime-process-scope STATIC
    src/process_scope.cpp
    src/process_scope_cleanup.cpp
    src/process_scope_preflight.cpp
    src/process_scope_start.cpp
    src/process_scope_stop.cpp
)
add_library(asharia::host_runtime_process_scope ALIAS
    asharia-host-runtime-process-scope)

target_include_directories(asharia-host-runtime-process-scope
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-host-runtime-process-scope
    PUBLIC
        asharia::host_runtime_activation_eligibility
)

asharia_configure_target(asharia-host-runtime-process-scope)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-host-runtime-registration-tests
      tests/static_factory_callback_table_tests.cpp
      tests/static_factory_registration_tests.cpp
  )

  target_link_libraries(asharia-host-runtime-registration-tests
      PRIVATE
          asharia::host_runtime_provider_bridge
          asharia::host_runtime_registration
  )

  target_include_directories(asharia-host-runtime-registration-tests
      PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )

  asharia_configure_target(asharia-host-runtime-registration-tests)

  add_test(
      NAME asharia-host-runtime-registration-tests
      COMMAND asharia-host-runtime-registration-tests
  )

  add_test(
      NAME asharia-host-runtime-valid-token-drop-tests
      COMMAND asharia-host-runtime-registration-tests --probe-valid-token-drop
  )

  set_tests_properties(asharia-host-runtime-valid-token-drop-tests
      PROPERTIES
          WILL_FAIL TRUE
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

  add_executable(asharia-host-runtime-activation-eligibility-tests
      tests/activation_eligibility_test_support.cpp
      tests/activation_eligibility_tests.cpp
      tests/admitted_static_factory_recording_tests.cpp
      tests/activation_admission_tests.cpp
  )

  target_include_directories(asharia-host-runtime-activation-eligibility-tests
      PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )

  target_link_libraries(asharia-host-runtime-activation-eligibility-tests
      PRIVATE
          asharia::host_runtime_activation_eligibility
  )

  asharia_configure_target(asharia-host-runtime-activation-eligibility-tests)

  add_test(
      NAME asharia-host-runtime-activation-eligibility-tests
      COMMAND asharia-host-runtime-activation-eligibility-tests
  )

  set_tests_properties(asharia-host-runtime-activation-eligibility-tests
      PROPERTIES
          TIMEOUT 30
  )

  add_library(asharia-host-runtime-process-scope-test-provider STATIC
      tests/process_scope_synthetic_provider.cpp
  )

  target_include_directories(asharia-host-runtime-process-scope-test-provider
      PUBLIC
          "${CMAKE_CURRENT_SOURCE_DIR}/tests"
  )

  target_link_libraries(asharia-host-runtime-process-scope-test-provider
      PRIVATE
          asharia::host_runtime_provider_bridge
          asharia::host_runtime_registration
  )

  asharia_configure_target(asharia-host-runtime-process-scope-test-provider)

  add_executable(asharia-host-runtime-process-scope-tests
      tests/process_scope_cleanup_tests.cpp
      tests/process_scope_contract_tests.cpp
      tests/process_scope_preflight_tests.cpp
      tests/process_scope_startup_tests.cpp
      tests/process_scope_test_support.cpp
      tests/process_scope_tests.cpp
  )

  target_include_directories(asharia-host-runtime-process-scope-tests
      PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )

  target_link_libraries(asharia-host-runtime-process-scope-tests
      PRIVATE
          asharia::host_runtime_process_scope
          asharia-host-runtime-process-scope-test-provider
  )

  asharia_configure_target(asharia-host-runtime-process-scope-tests)

  add_test(
      NAME asharia-host-runtime-process-scope-tests
      COMMAND asharia-host-runtime-process-scope-tests
  )

  add_test(
      NAME asharia-host-runtime-active-process-scope-drop-tests
      COMMAND asharia-host-runtime-process-scope-tests --probe-active-scope-drop
  )

  set_tests_properties(
      asharia-host-runtime-process-scope-tests
      asharia-host-runtime-active-process-scope-drop-tests
      PROPERTIES
          TIMEOUT 30
  )

  set_tests_properties(asharia-host-runtime-active-process-scope-drop-tests
      PROPERTIES
          PASS_REGULAR_EXPRESSION
              "asharia-process-scope-active-drop-terminated"
  )
endif()
