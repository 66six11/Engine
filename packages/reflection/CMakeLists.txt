cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEngineReflection VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()
vke_require_package_target(vke::core engine/core)

add_library(vke-reflection STATIC
    src/type_registry.cpp
)

add_library(vke::reflection ALIAS vke-reflection)

target_include_directories(vke-reflection
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-reflection
    PUBLIC
        vke::core
)

vke_configure_target(vke-reflection)

if(VKE_BUILD_TESTS)
  enable_testing()

  add_executable(vke-reflection-smoke-tests
      tests/reflection_smoke_tests.cpp
  )

  target_link_libraries(vke-reflection-smoke-tests
      PRIVATE
          vke::reflection
  )

  vke_configure_target(vke-reflection-smoke-tests)

  add_test(NAME vke-reflection-smoke-tests COMMAND vke-reflection-smoke-tests)
endif()
