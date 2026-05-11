cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEngineSerialization VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()
vke_require_package_target(vke::core engine/core)
vke_require_package_target(vke::reflection packages/reflection)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(vke-serialization STATIC
    src/archive_value.cpp
    src/serializer.cpp
    src/text_archive.cpp
)

add_library(vke::serialization ALIAS vke-serialization)

target_include_directories(vke-serialization
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-serialization
    PUBLIC
        vke::core
        vke::reflection
    PRIVATE
        nlohmann_json::nlohmann_json
)

vke_configure_target(vke-serialization)

if(VKE_BUILD_TESTS)
  enable_testing()

  add_executable(vke-serialization-smoke-tests
      tests/serialization_smoke_tests.cpp
  )

  target_link_libraries(vke-serialization-smoke-tests
      PRIVATE
          vke::serialization
  )

  vke_configure_target(vke-serialization-smoke-tests)

  add_test(NAME vke-serialization-smoke-tests COMMAND vke-serialization-smoke-tests)
endif()
