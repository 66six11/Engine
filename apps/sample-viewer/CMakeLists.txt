add_executable(vke-sample-viewer
    src/main.cpp
)

add_executable(vke::sample_viewer ALIAS vke-sample-viewer)

find_program(VKE_GLSLC_EXECUTABLE
    NAMES glslc glslc.exe
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "C:/VulkanSDK/1.4.321.1/Bin"
)

find_program(VKE_SPIRV_VAL_EXECUTABLE
    NAMES spirv-val spirv-val.exe
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "C:/VulkanSDK/1.4.321.1/Bin"
)

if(NOT VKE_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc is required to build sample-viewer triangle shaders.")
endif()

if(NOT VKE_SPIRV_VAL_EXECUTABLE)
    message(FATAL_ERROR "spirv-val is required to validate sample-viewer triangle shaders.")
endif()

set(VKE_SAMPLE_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(VKE_SAMPLE_TRIANGLE_VERT_SPV "${VKE_SAMPLE_SHADER_OUTPUT_DIR}/basic_triangle.vert.spv")
set(VKE_SAMPLE_TRIANGLE_FRAG_SPV "${VKE_SAMPLE_SHADER_OUTPUT_DIR}/basic_triangle.frag.spv")

add_custom_command(
    OUTPUT "${VKE_SAMPLE_TRIANGLE_VERT_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VKE_SAMPLE_SHADER_OUTPUT_DIR}"
    COMMAND "${VKE_GLSLC_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/packages/renderer-basic/shaders/basic_triangle.vert"
            -o "${VKE_SAMPLE_TRIANGLE_VERT_SPV}"
    COMMAND "${VKE_SPIRV_VAL_EXECUTABLE}" "${VKE_SAMPLE_TRIANGLE_VERT_SPV}"
    DEPENDS "${CMAKE_SOURCE_DIR}/packages/renderer-basic/shaders/basic_triangle.vert"
    VERBATIM
)

add_custom_command(
    OUTPUT "${VKE_SAMPLE_TRIANGLE_FRAG_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VKE_SAMPLE_SHADER_OUTPUT_DIR}"
    COMMAND "${VKE_GLSLC_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/packages/renderer-basic/shaders/basic_triangle.frag"
            -o "${VKE_SAMPLE_TRIANGLE_FRAG_SPV}"
    COMMAND "${VKE_SPIRV_VAL_EXECUTABLE}" "${VKE_SAMPLE_TRIANGLE_FRAG_SPV}"
    DEPENDS "${CMAKE_SOURCE_DIR}/packages/renderer-basic/shaders/basic_triangle.frag"
    VERBATIM
)

add_custom_target(vke-sample-viewer-shaders
    DEPENDS
        "${VKE_SAMPLE_TRIANGLE_VERT_SPV}"
        "${VKE_SAMPLE_TRIANGLE_FRAG_SPV}"
)

add_dependencies(vke-sample-viewer vke-sample-viewer-shaders)

target_link_libraries(vke-sample-viewer
    PRIVATE
        vke::core
        vke::rendergraph
        vke::rhi_vulkan
        vke::rhi_vulkan_rendergraph
        vke::window_glfw
        vke::renderer_basic
        vke::renderer_basic_vulkan
)

target_compile_definitions(vke-sample-viewer
    PRIVATE
        VKE_SAMPLE_SHADER_OUTPUT_DIR="${VKE_SAMPLE_SHADER_OUTPUT_DIR}"
)

vke_configure_target(vke-sample-viewer)
