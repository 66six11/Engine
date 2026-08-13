cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineEditorContent VERSION 0.1.0 LANGUAGES C CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::asset_core packages/asset-core)
asharia_require_package_target(asharia::asset_core_io packages/asset-core)
asharia_require_package_target(asharia::asset_pipeline packages/asset-pipeline)
asharia_require_package_target(asharia::project_core_io packages/project-core)

add_library(asharia-editor-content STATIC
    src/asset_catalog_snapshot.cpp
)
add_library(asharia::editor_content ALIAS asharia-editor-content)
set_target_properties(asharia-editor-content PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(asharia-editor-content PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(asharia-editor-content
    PUBLIC asharia::asset_core asharia::project_core_io
    PRIVATE asharia::asset_core_io asharia::asset_pipeline
)
asharia_configure_target(asharia-editor-content)

add_library(asharia-editor-content-native SHARED
    src/asset_catalog_native_api.cpp
    src/asset_catalog_snapshot_json.cpp
)
add_library(asharia::editor_content_native ALIAS asharia-editor-content-native)
set_target_properties(asharia-editor-content-native PROPERTIES OUTPUT_NAME asharia_editor_content_native)
target_include_directories(asharia-editor-content-native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(asharia-editor-content-native PRIVATE asharia::editor_content)
target_compile_definitions(asharia-editor-content-native PRIVATE ASHARIA_EDITOR_CONTENT_NATIVE_BUILD=1)
asharia_configure_target(asharia-editor-content-native)

if(ASHARIA_BUILD_TESTS)
  enable_testing()
  if(NOT CMAKE_C_COMPILER_LOADED)
    enable_language(C)
  endif()
  add_executable(asharia-editor-content-native-smoke-tests
      tests/asset_catalog_native_smoke_tests.cpp
  )
  target_link_libraries(asharia-editor-content-native-smoke-tests
      PRIVATE asharia::editor_content_native asharia::project_core_io
  )
  asharia_configure_target(asharia-editor-content-native-smoke-tests)
  add_test(NAME asharia-editor-content-native-smoke-tests COMMAND asharia-editor-content-native-smoke-tests)

  add_executable(asharia-editor-content-native-c-header-smoke
      tests/asset_catalog_native_c_header_smoke.c
  )
  target_include_directories(asharia-editor-content-native-c-header-smoke
      PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  add_test(NAME asharia-editor-content-native-c-header-smoke
      COMMAND asharia-editor-content-native-c-header-smoke)
endif()
