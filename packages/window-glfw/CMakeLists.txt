cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEngineWindowGlfw VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()
vke_require_package_target(vke::core engine/core)
vke_require_package_target(vke::platform engine/platform)

find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)

add_library(vke-window-glfw STATIC
    src/glfw_window.cpp
)

add_library(vke::window_glfw ALIAS vke-window-glfw)

target_include_directories(vke-window-glfw
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-window-glfw
    PUBLIC
        vke::core
        vke::platform
        vulkan-headers::vulkan-headers
        glfw
)

target_compile_definitions(vke-window-glfw PUBLIC GLFW_INCLUDE_NONE)

vke_configure_target(vke-window-glfw)
