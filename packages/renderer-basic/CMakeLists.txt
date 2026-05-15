cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineRendererBasic VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)
asharia_require_package_target(asharia::rendergraph packages/rendergraph)
asharia_require_package_target(asharia::shader_slang packages/shader-slang)
asharia_require_package_target(asharia::rhi_vulkan packages/rhi-vulkan)

add_library(asharia-renderer-basic INTERFACE)
add_library(asharia::renderer_basic ALIAS asharia-renderer-basic)
add_library(asharia-renderer-basic-vulkan
    src/basic_triangle_renderer.cpp
)
add_library(asharia::renderer_basic_vulkan ALIAS asharia-renderer-basic-vulkan)

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

asharia_add_slang_shader(asharia-renderer-basic-compute-comp
    SOURCE "${ASHARIA_RENDERER_BASIC_COMPUTE_SHADER_SOURCE}"
    ENTRY computeMain
    STAGE compute
    OUTPUT "${ASHARIA_RENDERER_BASIC_COMPUTE_COMP_SPV}"
    METADATA_OUTPUT "${ASHARIA_RENDERER_BASIC_COMPUTE_COMP_METADATA}"
    REFLECTION_OUTPUT "${ASHARIA_RENDERER_BASIC_COMPUTE_COMP_REFLECTION}"
    DEPENDS "${ASHARIA_RENDERER_BASIC_COMPUTE_SHADER_MANIFEST}"
)

add_custom_target(asharia-renderer-basic-shaders
    DEPENDS
        asharia-renderer-basic-triangle-vert
        asharia-renderer-basic-triangle-frag
        asharia-renderer-basic-descriptor-vert
        asharia-renderer-basic-descriptor-frag
        asharia-renderer-basic-mesh3d-vert
        asharia-renderer-basic-mesh3d-frag
        asharia-renderer-basic-compute-comp
)

target_include_directories(asharia-renderer-basic
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-renderer-basic
    INTERFACE
        asharia::core
        asharia::rendergraph
        asharia::shader_slang
)

target_include_directories(asharia-renderer-basic-vulkan
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-renderer-basic-vulkan
    PUBLIC
        asharia::renderer_basic
        asharia::rhi_vulkan
        asharia::rhi_vulkan_rendergraph
)

target_compile_definitions(asharia-renderer-basic
    INTERFACE
        ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR="${ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR}"
)

asharia_configure_target(asharia-renderer-basic)
asharia_configure_target(asharia-renderer-basic-vulkan)
