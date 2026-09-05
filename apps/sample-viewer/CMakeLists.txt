cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineSampleViewer VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::asset_pipeline packages/asset-pipeline)
asharia_require_package_target(asharia::mesh_product_writer packages/mesh-product)
asharia_require_package_target(asharia::resource_runtime packages/resource-runtime)
asharia_require_package_target(asharia::profiling packages/profiling)
asharia_require_package_target(asharia::reflection packages/reflection)
asharia_require_package_target(asharia::scene_core packages/scene-core)
asharia_require_package_target(asharia::scene_rendering packages/scene-rendering)
asharia_require_package_target(asharia::serialization packages/serialization)
asharia_require_package_target(asharia::window_glfw packages/window-glfw)
asharia_require_package_target(asharia::renderer_basic_vulkan packages/renderer-basic)
asharia_require_package_target(asharia::shader_authoring packages/shader-authoring)
asharia_require_package_target(asharia::shader_material_adapter packages/shader-material-adapter)
asharia_require_package_target(asharia::material_instance packages/material-instance)

add_executable(asharia-material-shader-fixture src/material_shader_fixture.cpp)
target_link_libraries(asharia-material-shader-fixture PRIVATE asharia::core asharia::shader_authoring)
asharia_configure_target(asharia-material-shader-fixture)
set(material_fixture_dir "${ASHARIA_REPOSITORY_ROOT}/assets/fixtures/material-authoring")
set(material_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/material-fixture")
add_custom_command(
    OUTPUT "${material_generated_dir}/numeric-unlit.slang"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${material_generated_dir}"
    COMMAND "$<TARGET_FILE:asharia-material-shader-fixture>"
        "${material_fixture_dir}/numeric-unlit.ashader" "${material_generated_dir}/numeric-unlit.slang"
    DEPENDS asharia-material-shader-fixture "${material_fixture_dir}/numeric-unlit.ashader"
    VERBATIM
)
asharia_add_slang_shader(asharia-material-fixture-fragment
    SOURCE "${material_generated_dir}/numeric-unlit.slang"
    ENTRY fragmentMain STAGE fragment PROFILE glsl_450
    OUTPUT "${material_generated_dir}/numeric-unlit.frag.spv"
    REFLECTION_OUTPUT "${material_generated_dir}/numeric-unlit.frag.reflection.json"
)

add_executable(asharia-sample-viewer
    src/main.cpp
    src/scene_mesh_smoke.cpp
)

add_executable(asharia::sample_viewer ALIAS asharia-sample-viewer)

add_dependencies(asharia-sample-viewer asharia-renderer-basic-shaders)
add_dependencies(asharia-sample-viewer asharia-material-fixture-fragment)
target_compile_definitions(asharia-sample-viewer PRIVATE
    ASHARIA_MATERIAL_FIXTURE_DIR="${material_fixture_dir}"
    ASHARIA_MATERIAL_GENERATED_DIR="${material_generated_dir}"
)

target_link_libraries(asharia-sample-viewer
    PRIVATE
        asharia::asset_pipeline
        asharia::mesh_product_writer
        asharia::resource_runtime
        asharia::core
        asharia::profiling
        asharia::reflection
        asharia::rendergraph
        asharia::rhi_vulkan
        asharia::rhi_vulkan_rendergraph
        asharia::scene_core
        asharia::scene_rendering
        asharia::serialization
        asharia::window_glfw
        asharia::renderer_basic
        asharia::renderer_basic_vulkan
        asharia::shader_authoring
        asharia::shader_material_adapter
        asharia::material_instance
        asharia::shader_slang
)

asharia_configure_target(asharia-sample-viewer)
