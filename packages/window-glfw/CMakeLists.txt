cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineWindowGlfw VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::platform engine/platform)

find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)

add_library(asharia-window-glfw STATIC
    src/glfw_window.cpp
)

add_library(asharia::window_glfw ALIAS asharia-window-glfw)

target_include_directories(asharia-window-glfw
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-window-glfw
    PUBLIC
        asharia::core
        asharia::platform
        vulkan-headers::vulkan-headers
        glfw
)

target_compile_definitions(asharia-window-glfw PUBLIC GLFW_INCLUDE_NONE)

asharia_configure_target(asharia-window-glfw)
