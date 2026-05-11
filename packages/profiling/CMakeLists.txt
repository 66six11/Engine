cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEngineProfiling VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()

add_library(vke-profiling INTERFACE)
add_library(vke::profiling ALIAS vke-profiling)

target_include_directories(vke-profiling
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

vke_configure_target(vke-profiling)
