add_library(vke-renderer-basic INTERFACE)
add_library(vke::renderer_basic ALIAS vke-renderer-basic)
add_library(vke-renderer-basic-vulkan
    src/basic_triangle_renderer.cpp
)
add_library(vke::renderer_basic_vulkan ALIAS vke-renderer-basic-vulkan)

set(VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(VKE_RENDERER_BASIC_TRIANGLE_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_triangle.slang"
)
set(VKE_RENDERER_BASIC_TRIANGLE_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_triangle.shader.json"
)
set(VKE_RENDERER_BASIC_TRIANGLE_VERT_SPV
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.vert.spv"
)
set(VKE_RENDERER_BASIC_TRIANGLE_VERT_METADATA
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.vert.metadata.json"
)
set(VKE_RENDERER_BASIC_TRIANGLE_VERT_REFLECTION
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.vert.reflection.json"
)
set(VKE_RENDERER_BASIC_TRIANGLE_FRAG_SPV
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.frag.spv"
)
set(VKE_RENDERER_BASIC_TRIANGLE_FRAG_METADATA
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.frag.metadata.json"
)
set(VKE_RENDERER_BASIC_TRIANGLE_FRAG_REFLECTION
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.frag.reflection.json"
)
set(VKE_RENDERER_BASIC_DESCRIPTOR_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/descriptor_layout.slang"
)
set(VKE_RENDERER_BASIC_DESCRIPTOR_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/descriptor_layout.shader.json"
)
set(VKE_RENDERER_BASIC_DESCRIPTOR_VERT_SPV
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.vert.spv"
)
set(VKE_RENDERER_BASIC_DESCRIPTOR_VERT_METADATA
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.vert.metadata.json"
)
set(VKE_RENDERER_BASIC_DESCRIPTOR_VERT_REFLECTION
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.vert.reflection.json"
)
set(VKE_RENDERER_BASIC_DESCRIPTOR_FRAG_SPV
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.frag.spv"
)
set(VKE_RENDERER_BASIC_DESCRIPTOR_FRAG_METADATA
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.frag.metadata.json"
)
set(VKE_RENDERER_BASIC_DESCRIPTOR_FRAG_REFLECTION
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/descriptor_layout.frag.reflection.json"
)
set(VKE_RENDERER_BASIC_MESH3D_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_mesh3d.slang"
)
set(VKE_RENDERER_BASIC_MESH3D_SHADER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic_mesh3d.shader.json"
)
set(VKE_RENDERER_BASIC_MESH3D_VERT_SPV
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.vert.spv"
)
set(VKE_RENDERER_BASIC_MESH3D_VERT_METADATA
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.vert.metadata.json"
)
set(VKE_RENDERER_BASIC_MESH3D_VERT_REFLECTION
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.vert.reflection.json"
)
set(VKE_RENDERER_BASIC_MESH3D_FRAG_SPV
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.frag.spv"
)
set(VKE_RENDERER_BASIC_MESH3D_FRAG_METADATA
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.frag.metadata.json"
)
set(VKE_RENDERER_BASIC_MESH3D_FRAG_REFLECTION
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_mesh3d.frag.reflection.json"
)

vke_add_slang_shader(vke-renderer-basic-triangle-vert
    SOURCE "${VKE_RENDERER_BASIC_TRIANGLE_SHADER_SOURCE}"
    ENTRY vertexMain
    STAGE vertex
    OUTPUT "${VKE_RENDERER_BASIC_TRIANGLE_VERT_SPV}"
    METADATA_OUTPUT "${VKE_RENDERER_BASIC_TRIANGLE_VERT_METADATA}"
    REFLECTION_OUTPUT "${VKE_RENDERER_BASIC_TRIANGLE_VERT_REFLECTION}"
    DEPENDS "${VKE_RENDERER_BASIC_TRIANGLE_SHADER_MANIFEST}"
)

vke_add_slang_shader(vke-renderer-basic-triangle-frag
    SOURCE "${VKE_RENDERER_BASIC_TRIANGLE_SHADER_SOURCE}"
    ENTRY fragmentMain
    STAGE fragment
    OUTPUT "${VKE_RENDERER_BASIC_TRIANGLE_FRAG_SPV}"
    METADATA_OUTPUT "${VKE_RENDERER_BASIC_TRIANGLE_FRAG_METADATA}"
    REFLECTION_OUTPUT "${VKE_RENDERER_BASIC_TRIANGLE_FRAG_REFLECTION}"
    DEPENDS "${VKE_RENDERER_BASIC_TRIANGLE_SHADER_MANIFEST}"
)

vke_add_slang_shader(vke-renderer-basic-descriptor-vert
    SOURCE "${VKE_RENDERER_BASIC_DESCRIPTOR_SHADER_SOURCE}"
    ENTRY descriptorVertexMain
    STAGE vertex
    OUTPUT "${VKE_RENDERER_BASIC_DESCRIPTOR_VERT_SPV}"
    METADATA_OUTPUT "${VKE_RENDERER_BASIC_DESCRIPTOR_VERT_METADATA}"
    REFLECTION_OUTPUT "${VKE_RENDERER_BASIC_DESCRIPTOR_VERT_REFLECTION}"
    DEPENDS "${VKE_RENDERER_BASIC_DESCRIPTOR_SHADER_MANIFEST}"
)

vke_add_slang_shader(vke-renderer-basic-descriptor-frag
    SOURCE "${VKE_RENDERER_BASIC_DESCRIPTOR_SHADER_SOURCE}"
    ENTRY descriptorFragmentMain
    STAGE fragment
    OUTPUT "${VKE_RENDERER_BASIC_DESCRIPTOR_FRAG_SPV}"
    METADATA_OUTPUT "${VKE_RENDERER_BASIC_DESCRIPTOR_FRAG_METADATA}"
    REFLECTION_OUTPUT "${VKE_RENDERER_BASIC_DESCRIPTOR_FRAG_REFLECTION}"
    DEPENDS "${VKE_RENDERER_BASIC_DESCRIPTOR_SHADER_MANIFEST}"
)

vke_add_slang_shader(vke-renderer-basic-mesh3d-vert
    SOURCE "${VKE_RENDERER_BASIC_MESH3D_SHADER_SOURCE}"
    ENTRY mesh3DVertexMain
    STAGE vertex
    OUTPUT "${VKE_RENDERER_BASIC_MESH3D_VERT_SPV}"
    METADATA_OUTPUT "${VKE_RENDERER_BASIC_MESH3D_VERT_METADATA}"
    REFLECTION_OUTPUT "${VKE_RENDERER_BASIC_MESH3D_VERT_REFLECTION}"
    DEPENDS "${VKE_RENDERER_BASIC_MESH3D_SHADER_MANIFEST}"
)

vke_add_slang_shader(vke-renderer-basic-mesh3d-frag
    SOURCE "${VKE_RENDERER_BASIC_MESH3D_SHADER_SOURCE}"
    ENTRY mesh3DFragmentMain
    STAGE fragment
    OUTPUT "${VKE_RENDERER_BASIC_MESH3D_FRAG_SPV}"
    METADATA_OUTPUT "${VKE_RENDERER_BASIC_MESH3D_FRAG_METADATA}"
    REFLECTION_OUTPUT "${VKE_RENDERER_BASIC_MESH3D_FRAG_REFLECTION}"
    DEPENDS "${VKE_RENDERER_BASIC_MESH3D_SHADER_MANIFEST}"
)

add_custom_target(vke-renderer-basic-shaders
    DEPENDS
        vke-renderer-basic-triangle-vert
        vke-renderer-basic-triangle-frag
        vke-renderer-basic-descriptor-vert
        vke-renderer-basic-descriptor-frag
        vke-renderer-basic-mesh3d-vert
        vke-renderer-basic-mesh3d-frag
)

target_include_directories(vke-renderer-basic
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-renderer-basic
    INTERFACE
        vke::core
        vke::rendergraph
        vke::shader_slang
)

target_include_directories(vke-renderer-basic-vulkan
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-renderer-basic-vulkan
    PUBLIC
        vke::renderer_basic
        vke::rhi_vulkan
        vke::rhi_vulkan_rendergraph
)

target_compile_definitions(vke-renderer-basic
    INTERFACE
        VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR="${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}"
)

vke_configure_target(vke-renderer-basic)
vke_configure_target(vke-renderer-basic-vulkan)
