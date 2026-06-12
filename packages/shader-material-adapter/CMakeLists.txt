cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineShaderMaterialAdapter VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::material_core packages/material-core)
asharia_require_package_target(asharia::shader_slang packages/shader-slang)

add_library(asharia-shader-material-adapter STATIC
    src/reflection_to_material_signature.cpp
)
add_library(asharia::shader_material_adapter ALIAS asharia-shader-material-adapter)

target_include_directories(asharia-shader-material-adapter
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-shader-material-adapter
    PUBLIC
        asharia::core
        asharia::material_core
        asharia::shader_slang
)

asharia_configure_target(asharia-shader-material-adapter)

if(ASHARIA_BUILD_TESTS)
  enable_testing()

  add_executable(asharia-shader-material-adapter-smoke-tests
      tests/reflection_to_material_signature_smoke_tests.cpp
  )

  target_link_libraries(asharia-shader-material-adapter-smoke-tests
      PRIVATE
          asharia::shader_material_adapter
  )

  asharia_configure_target(asharia-shader-material-adapter-smoke-tests)

  add_test(NAME asharia-shader-material-adapter-smoke-tests
           COMMAND asharia-shader-material-adapter-smoke-tests)
endif()
