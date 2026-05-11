cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEngineSampleViewer VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()
vke_require_package_target(vke::profiling packages/profiling)
vke_require_package_target(vke::reflection packages/reflection)
vke_require_package_target(vke::serialization packages/serialization)
vke_require_package_target(vke::window_glfw packages/window-glfw)
vke_require_package_target(vke::renderer_basic_vulkan packages/renderer-basic)

add_executable(vke-sample-viewer
    src/main.cpp
)

add_executable(vke::sample_viewer ALIAS vke-sample-viewer)

add_dependencies(vke-sample-viewer vke-renderer-basic-shaders)

target_link_libraries(vke-sample-viewer
    PRIVATE
        vke::core
        vke::profiling
        vke::reflection
        vke::rendergraph
        vke::rhi_vulkan
        vke::rhi_vulkan_rendergraph
        vke::serialization
        vke::window_glfw
        vke::renderer_basic
        vke::renderer_basic_vulkan
)

vke_configure_target(vke-sample-viewer)
