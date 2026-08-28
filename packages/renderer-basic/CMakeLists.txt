cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineRendererBasic VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::material_core packages/material-core)
asharia_require_package_target(asharia::rendergraph packages/rendergraph)
asharia_require_package_target(asharia::shader_slang packages/shader-slang)
asharia_require_package_target(asharia::rhi_vulkan packages/rhi-vulkan)

add_library(asharia-renderer-basic INTERFACE)
add_library(asharia::renderer_basic ALIAS asharia-renderer-basic)

set(ASHARIA_RENDERER_BASIC_GENERATED_INCLUDE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
set(ASHARIA_RENDERER_BASIC_VALIDATION_MESH_HEADER
    "${ASHARIA_RENDERER_BASIC_GENERATED_INCLUDE_DIR}/asharia/validation/directional_wedge_mesh_product.hpp"
)
set(ASHARIA_RENDERER_BASIC_VALIDATION_MESH_MANIFEST
    "${ASHARIA_RENDERER_BASIC_GENERATED_INCLUDE_DIR}/asharia/validation/directional_wedge_mesh_product.json"
)
set(ASHARIA_RENDERER_BASIC_VALIDATION_MESH_SOURCE
    "${ASHARIA_REPOSITORY_ROOT}/assets/fixtures/scene-rendering/directional-wedge.obj"
)
set(ASHARIA_RENDERER_BASIC_VALIDATION_MESH_METADATA
    "${ASHARIA_REPOSITORY_ROOT}/assets/fixtures/scene-rendering/directional-wedge.obj.ameta"
)
set(ASHARIA_RENDERER_BASIC_VALIDATION_MESH_GENERATOR
    "${ASHARIA_REPOSITORY_ROOT}/tools/generate_validation_mesh_product.py"
)
add_custom_command(
    OUTPUT
        "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_HEADER}"
        "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_MANIFEST}"
    COMMAND
        "${CMAKE_COMMAND}" -E make_directory
        "${ASHARIA_RENDERER_BASIC_GENERATED_INCLUDE_DIR}/asharia/validation"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_GENERATOR}"
        --source "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_SOURCE}"
        --metadata "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_METADATA}"
        --output-header "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_HEADER}"
        --output-manifest "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_MANIFEST}"
    DEPENDS
        "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_GENERATOR}"
        "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_SOURCE}"
        "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_METADATA}"
    VERBATIM
)
add_custom_target(asharia-renderer-basic-validation-mesh-product
    DEPENDS
        "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_HEADER}"
        "${ASHARIA_RENDERER_BASIC_VALIDATION_MESH_MANIFEST}"
)
set(ASHARIA_RENDERER_BASIC_VULKAN_PRIVATE_PARTS
    src/basic_renderers/compute_dispatch_renderer.inl
    src/basic_renderers/debug_preview.inl
    src/basic_renderers/descriptor_layout_smoke.inl
    src/basic_renderers/draw_list_renderer.inl
    src/basic_renderers/fullscreen_texture_renderer.inl
    src/basic_renderers/graph_recording.inl
    src/basic_renderers/material_binding_smoke.inl
    src/basic_renderers/mesh3d_renderer.inl
    src/basic_renderers/mrt_renderer.inl
    src/basic_renderers/pipeline_layouts.inl
    src/basic_renderers/render_view_diagnostics.inl
    src/basic_renderers/render_view_pass_policy.inl
    src/basic_renderers/render_view_recording.inl
    src/basic_renderers/render_view_targets.inl
    src/basic_renderers/shader_contracts.inl
    src/basic_renderers/triangle_renderer.inl
)
set_source_files_properties(
    ${ASHARIA_RENDERER_BASIC_VULKAN_PRIVATE_PARTS}
    PROPERTIES HEADER_FILE_ONLY TRUE
)
add_library(asharia-renderer-basic-vulkan
    src/basic_renderers.cpp
    ${ASHARIA_RENDERER_BASIC_VULKAN_PRIVATE_PARTS}
)
add_library(asharia::renderer_basic_vulkan ALIAS asharia-renderer-basic-vulkan)
add_dependencies(asharia-renderer-basic-vulkan
    asharia-renderer-basic-validation-mesh-product
)

set(ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(ASHARIA_RENDERER_BASIC_TRIANGLE_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_triangle.slang"
)
set(ASHARIA_RENDERER_BASIC_TRIANGLE_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_triangle.shader.json"
)
set(ASHARIA_RENDERER_BASIC_TRIANGLE_VERT_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.vert.spv"
)
set(ASHARIA_RENDERER_BASIC_TRIANGLE_VERT_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.vert.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_TRIANGLE_VERT_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.vert.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_TRIANGLE_FRAG_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.frag.spv"
)
set(ASHARIA_RENDERER_BASIC_TRIANGLE_FRAG_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.frag.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_TRIANGLE_FRAG_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.frag.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_DESCRIPTOR_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/descriptor_layout.slang"
)
set(ASHARIA_RENDERER_BASIC_DESCRIPTOR_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/descriptor_layout.shader.json"
)
set(ASHARIA_RENDERER_BASIC_DESCRIPTOR_VERT_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.vert.spv"
)
set(ASHARIA_RENDERER_BASIC_DESCRIPTOR_VERT_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.vert.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_DESCRIPTOR_VERT_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.vert.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_DESCRIPTOR_FRAG_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.frag.spv"
)
set(ASHARIA_RENDERER_BASIC_DESCRIPTOR_FRAG_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.frag.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_DESCRIPTOR_FRAG_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.frag.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_MESH3D_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_mesh3d.slang"
)
set(ASHARIA_RENDERER_BASIC_MESH3D_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_mesh3d.shader.json"
)
set(ASHARIA_RENDERER_BASIC_MESH3D_VERT_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.vert.spv"
)
set(ASHARIA_RENDERER_BASIC_MESH3D_VERT_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.vert.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_MESH3D_VERT_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.vert.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_MESH3D_FRAG_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.frag.spv"
)
set(ASHARIA_RENDERER_BASIC_MESH3D_FRAG_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.frag.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_MESH3D_FRAG_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.frag.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_DEBUG_LINE_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_line.slang"
)
set(ASHARIA_RENDERER_BASIC_DEBUG_LINE_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_line.shader.json"
)
set(ASHARIA_RENDERER_BASIC_WORLD_GRID_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world_grid.slang"
)
set(ASHARIA_RENDERER_BASIC_WORLD_GRID_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world_grid.shader.json"
)
set(ASHARIA_RENDERER_BASIC_WORLD_GRID_VERT_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/world_grid.vert.spv"
)
set(ASHARIA_RENDERER_BASIC_WORLD_GRID_VERT_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/world_grid.vert.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_WORLD_GRID_VERT_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/world_grid.vert.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_WORLD_GRID_FRAG_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/world_grid.frag.spv"
)
set(ASHARIA_RENDERER_BASIC_WORLD_GRID_FRAG_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/world_grid.frag.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_WORLD_GRID_FRAG_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/world_grid.frag.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_DEBUG_LINE_VERT_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/debug_line.vert.spv"
)
set(ASHARIA_RENDERER_BASIC_DEBUG_LINE_VERT_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/debug_line.vert.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_DEBUG_LINE_VERT_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/debug_line.vert.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_DEBUG_LINE_FRAG_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/debug_line.frag.spv"
)
set(ASHARIA_RENDERER_BASIC_DEBUG_LINE_FRAG_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/debug_line.frag.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_DEBUG_LINE_FRAG_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/debug_line.frag.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_COMPUTE_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_compute.slang"
)
set(ASHARIA_RENDERER_BASIC_COMPUTE_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_compute.shader.json"
)
set(ASHARIA_RENDERER_BASIC_COMPUTE_COMP_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_compute.comp.spv"
)
set(ASHARIA_RENDERER_BASIC_COMPUTE_COMP_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_compute.comp.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_COMPUTE_COMP_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_compute.comp.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_outline.slang"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_MASK_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_mask.slang"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_MASK_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_mask.shader.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_outline.shader.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERTEX_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_outline_vertex.slang"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERTEX_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/selection_outline_vertex.shader.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_MASK_FRAG_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_mask.frag.spv"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_MASK_FRAG_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_mask.frag.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_MASK_FRAG_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_mask.frag.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERT_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_outline.vert.spv"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERT_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_outline.vert.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERT_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_outline.vert.reflection.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_FRAG_SPV
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_outline.frag.spv"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_FRAG_METADATA
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_outline.frag.metadata.json"
)
set(ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_FRAG_REFLECTION
    "${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}/selection_outline.frag.reflection.json"
)

asharia_add_slang_shader(asharia-renderer-basic-triangle-vert
    SOURCE "${ASHARIA_RENDERER_BASIC_TRIANGLE_SHADER_SOURCE}"
    ENTRY vertexMain
    STAGE vertex
    OUTPUT "${ASHARIA_RENDERER_BASIC_TRIANGLE_VERT_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_TRIANGLE_VERT_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_TRIANGLE_VERT_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_TRIANGLE_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-triangle-frag
    SOURCE "${ASHARIA_RENDERER_BASIC_TRIANGLE_SHADER_SOURCE}"
    ENTRY fragmentMain
    STAGE fragment
    OUTPUT "${ASHARIA_RENDERER_BASIC_TRIANGLE_FRAG_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_TRIANGLE_FRAG_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_TRIANGLE_FRAG_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_TRIANGLE_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-descriptor-vert
    SOURCE "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_SHADER_SOURCE}"
    ENTRY descriptorVertexMain
    STAGE vertex
    OUTPUT "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_VERT_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_VERT_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_VERT_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-descriptor-frag
    SOURCE "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_SHADER_SOURCE}"
    ENTRY descriptorFragmentMain
    STAGE fragment
    OUTPUT "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_FRAG_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_FRAG_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_FRAG_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_DESCRIPTOR_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-mesh3d-vert
    SOURCE "${ASHARIA_RENDERER_BASIC_MESH3D_SHADER_SOURCE}"
    ENTRY mesh3DVertexMain
    STAGE vertex
    OUTPUT "${ASHARIA_RENDERER_BASIC_MESH3D_VERT_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_MESH3D_VERT_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_MESH3D_VERT_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_MESH3D_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-mesh3d-frag
    SOURCE "${ASHARIA_RENDERER_BASIC_MESH3D_SHADER_SOURCE}"
    ENTRY mesh3DFragmentMain
    STAGE fragment
    OUTPUT "${ASHARIA_RENDERER_BASIC_MESH3D_FRAG_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_MESH3D_FRAG_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_MESH3D_FRAG_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_MESH3D_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-debug-line-vert
    SOURCE "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_SHADER_SOURCE}"
    ENTRY debugLineVertexMain
    STAGE vertex
    OUTPUT "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_VERT_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_VERT_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_VERT_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-debug-line-frag
    SOURCE "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_SHADER_SOURCE}"
    ENTRY debugLineFragmentMain
    STAGE fragment
    OUTPUT "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_FRAG_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_FRAG_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_FRAG_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_DEBUG_LINE_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-world-grid-vert
    SOURCE "${ASHARIA_RENDERER_BASIC_WORLD_GRID_SHADER_SOURCE}"
    ENTRY worldGridVertexMain
    STAGE vertex
    OUTPUT "${ASHARIA_RENDERER_BASIC_WORLD_GRID_VERT_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_WORLD_GRID_VERT_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_WORLD_GRID_VERT_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_WORLD_GRID_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-world-grid-frag
    SOURCE "${ASHARIA_RENDERER_BASIC_WORLD_GRID_SHADER_SOURCE}"
    ENTRY worldGridFragmentMain
    STAGE fragment
    OUTPUT "${ASHARIA_RENDERER_BASIC_WORLD_GRID_FRAG_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_WORLD_GRID_FRAG_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_WORLD_GRID_FRAG_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_WORLD_GRID_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-compute-comp
    SOURCE "${ASHARIA_RENDERER_BASIC_COMPUTE_SHADER_SOURCE}"
    ENTRY computeMain
    STAGE compute
    OUTPUT "${ASHARIA_RENDERER_BASIC_COMPUTE_COMP_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_COMPUTE_COMP_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_COMPUTE_COMP_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_COMPUTE_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-selection-mask-frag
    SOURCE "${ASHARIA_RENDERER_BASIC_SELECTION_MASK_SHADER_SOURCE}"
    ENTRY selectionMaskFragmentMain
    STAGE fragment
    OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_MASK_FRAG_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_MASK_FRAG_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_MASK_FRAG_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_SELECTION_MASK_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-selection-outline-vert
    SOURCE "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERTEX_SHADER_SOURCE}"
    ENTRY selectionOutlineVertexMain
    STAGE vertex
    OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERT_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERT_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERT_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_VERTEX_SHADER_MANIFEST}"
)

asharia_add_slang_shader(asharia-renderer-basic-selection-outline-frag
    SOURCE "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_SHADER_SOURCE}"
    ENTRY selectionOutlineFragmentMain
    STAGE fragment
    OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_FRAG_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_FRAG_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_FRAG_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_SELECTION_OUTLINE_SHADER_MANIFEST}"
)

add_custom_target(asharia-renderer-basic-shaders
    DEPENDS
        asharia-renderer-basic-triangle-vert
        asharia-renderer-basic-triangle-frag
        asharia-renderer-basic-descriptor-vert
        asharia-renderer-basic-descriptor-frag
        asharia-renderer-basic-mesh3d-vert
        asharia-renderer-basic-mesh3d-frag
        asharia-renderer-basic-debug-line-vert
        asharia-renderer-basic-debug-line-frag
        asharia-renderer-basic-world-grid-vert
        asharia-renderer-basic-world-grid-frag
        asharia-renderer-basic-compute-comp
        asharia-renderer-basic-selection-mask-frag
        asharia-renderer-basic-selection-outline-vert
        asharia-renderer-basic-selection-outline-frag
)

target_include_directories(asharia-renderer-basic
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-renderer-basic
    INTERFACE
        asharia::core
        asharia::rendergraph
)

target_include_directories(asharia-renderer-basic-vulkan
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${ASHARIA_RENDERER_BASIC_GENERATED_INCLUDE_DIR}"
)

target_link_libraries(asharia-renderer-basic-vulkan
    PUBLIC
        asharia::renderer_basic
        asharia::rhi_vulkan
        asharia::rhi_vulkan_rendergraph
    PRIVATE
        asharia::material_core
        asharia::shader_slang
)

target_compile_definitions(asharia-renderer-basic
    INTERFACE
        ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR="${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}"
)

asharia_configure_target(asharia-renderer-basic)
asharia_configure_target(asharia-renderer-basic-vulkan)
