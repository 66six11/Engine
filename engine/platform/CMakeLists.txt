cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEnginePlatform VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()
vke_require_package_target(vke::core engine/core)

add_library(vke-platform INTERFACE)
add_library(vke::platform ALIAS vke-platform)

target_include_directories(vke-platform
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-platform
    INTERFACE
        vke::core
)

vke_configure_target(vke-platform)
