cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineSampleViewer VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::profiling packages/profiling)
asharia_require_package_target(asharia::reflection packages/reflection)
asharia_require_package_target(asharia::serialization packages/serialization)
asharia_require_package_target(asharia::window_glfw packages/window-glfw)
asharia_require_package_target(asharia::renderer_basic_vulkan packages/renderer-basic)

add_executable(asharia-sample-viewer
    src/main.cpp
)

add_executable(asharia::sample_viewer ALIAS asharia-sample-viewer)

add_dependencies(asharia-sample-viewer asharia-renderer-basic-shaders)

target_link_libraries(asharia-sample-viewer
    PRIVATE
        asharia::core
        asharia::profiling
        asharia::reflection
        asharia::rendergraph
        asharia::rhi_vulkan
        asharia::rhi_vulkan_rendergraph
        asharia::serialization
        asharia::window_glfw
        asharia::renderer_basic
        asharia::renderer_basic_vulkan
)

asharia_configure_target(asharia-sample-viewer)
