add_library(vke-shader-slang INTERFACE)
add_library(vke::shader_slang ALIAS vke-shader-slang)

find_program(VKE_SLANGC_EXECUTABLE
    NAMES slangc slangc.exe
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

function(vke_add_slang_shader target)
    set(options)
    set(one_value_args SOURCE ENTRY STAGE OUTPUT PROFILE METADATA_OUTPUT)
    set(multi_value_args DEPENDS)
    cmake_parse_arguments(VKE_SLANG_SHADER
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    if(NOT VKE_SLANG_SHADER_SOURCE)
        message(FATAL_ERROR "vke_add_slang_shader requires SOURCE.")
    endif()
    if(NOT VKE_SLANG_SHADER_ENTRY)
        message(FATAL_ERROR "vke_add_slang_shader requires ENTRY.")
    endif()
    if(NOT VKE_SLANG_SHADER_STAGE)
        message(FATAL_ERROR "vke_add_slang_shader requires STAGE.")
    endif()
    if(NOT VKE_SLANG_SHADER_OUTPUT)
        message(FATAL_ERROR "vke_add_slang_shader requires OUTPUT.")
    endif()

    if(NOT VKE_SLANG_SHADER_PROFILE)
        set(VKE_SLANG_SHADER_PROFILE glsl_450)
    endif()

    if(NOT VKE_SLANGC_EXECUTABLE)
        message(FATAL_ERROR "slangc is required to build Slang shaders.")
    endif()
    if(NOT VKE_SPIRV_VAL_EXECUTABLE)
        message(FATAL_ERROR "spirv-val is required to validate Slang shader SPIR-V.")
    endif()

    get_filename_component(output_dir "${VKE_SLANG_SHADER_OUTPUT}" DIRECTORY)
    set(shader_outputs "${VKE_SLANG_SHADER_OUTPUT}")
    if(VKE_SLANG_SHADER_METADATA_OUTPUT)
        list(APPEND shader_outputs "${VKE_SLANG_SHADER_METADATA_OUTPUT}")
        set(metadata_command
            COMMAND "${CMAKE_COMMAND}"
                "-DVKE_SHADER_METADATA_OUTPUT=${VKE_SLANG_SHADER_METADATA_OUTPUT}"
                "-DVKE_SHADER_SOURCE=${VKE_SLANG_SHADER_SOURCE}"
                "-DVKE_SHADER_ENTRY=${VKE_SLANG_SHADER_ENTRY}"
                "-DVKE_SHADER_STAGE=${VKE_SLANG_SHADER_STAGE}"
                "-DVKE_SHADER_PROFILE=${VKE_SLANG_SHADER_PROFILE}"
                "-DVKE_SHADER_TARGET=spirv"
                "-DVKE_SHADER_OUTPUT=${VKE_SLANG_SHADER_OUTPUT}"
                "-DVKE_SLANGC_EXECUTABLE=${VKE_SLANGC_EXECUTABLE}"
                "-DVKE_SPIRV_VAL_EXECUTABLE=${VKE_SPIRV_VAL_EXECUTABLE}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmake/WriteShaderMetadata.cmake"
        )
    endif()

    add_custom_command(
        OUTPUT ${shader_outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
        COMMAND "${VKE_SLANGC_EXECUTABLE}"
                "${VKE_SLANG_SHADER_SOURCE}"
                -profile "${VKE_SLANG_SHADER_PROFILE}"
                -target spirv
                -entry "${VKE_SLANG_SHADER_ENTRY}"
                -stage "${VKE_SLANG_SHADER_STAGE}"
                -o "${VKE_SLANG_SHADER_OUTPUT}"
        COMMAND "${VKE_SPIRV_VAL_EXECUTABLE}" "${VKE_SLANG_SHADER_OUTPUT}"
        ${metadata_command}
        DEPENDS
            "${VKE_SLANG_SHADER_SOURCE}"
            ${VKE_SLANG_SHADER_DEPENDS}
        VERBATIM
    )

    add_custom_target("${target}" DEPENDS ${shader_outputs})
endfunction()

target_include_directories(vke-shader-slang
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-shader-slang
    INTERFACE
        vke::core
)

vke_configure_target(vke-shader-slang)
