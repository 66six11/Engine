add_library(vke-renderer-basic INTERFACE)
add_library(vke::renderer_basic ALIAS vke-renderer-basic)
add_library(vke-renderer-basic-vulkan INTERFACE)
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
set(VKE_RENDERER_BASIC_TRIANGLE_FRAG_SPV
    "${VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR}/basic_triangle.frag.spv"
)

vke_add_slang_shader(vke-renderer-basic-triangle-vert
    SOURCE "${VKE_RENDERER_BASIC_TRIANGLE_SHADER_SOURCE}"
    ENTRY vertexMain
    STAGE vertex
    OUTPUT "${VKE_RENDERER_BASIC_TRIANGLE_VERT_SPV}"
    DEPENDS "${VKE_RENDERER_BASIC_TRIANGLE_SHADER_MANIFEST}"
)

vke_add_slang_shader(vke-renderer-basic-triangle-frag
    SOURCE "${VKE_RENDERER_BASIC_TRIANGLE_SHADER_SOURCE}"
    ENTRY fragmentMain
    STAGE fragment
    OUTPUT "${VKE_RENDERER_BASIC_TRIANGLE_FRAG_SPV}"
    DEPENDS "${VKE_RENDERER_BASIC_TRIANGLE_SHADER_MANIFEST}"
)

add_custom_target(vke-renderer-basic-shaders
    DEPENDS
        vke-renderer-basic-triangle-vert
        vke-renderer-basic-triangle-frag
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

target_link_libraries(vke-renderer-basic-vulkan
    INTERFACE
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
