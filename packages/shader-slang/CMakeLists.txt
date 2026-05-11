cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEngineShaderSlang VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()
vke_require_package_target(vke::core engine/core)

add_library(vke-shader-slang
  src/shader_reflection.cpp
)
add_library(vke::shader_slang ALIAS vke-shader-slang)

find_path(VKE_SLANG_INCLUDE_DIR
    NAMES slang/slang.h
    HINTS
        "$ENV{VULKAN_SDK}/Include"
        "C:/VulkanSDK/1.4.321.1/Include"
    REQUIRED
)

find_library(VKE_SLANG_LIBRARY
    NAMES slang slang.lib
    HINTS
        "$ENV{VULKAN_SDK}/Lib"
        "C:/VulkanSDK/1.4.321.1/Lib"
    REQUIRED
)

find_file(VKE_SLANG_DLL
    NAMES slang.dll
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "C:/VulkanSDK/1.4.321.1/Bin"
    REQUIRED
)

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

add_executable(vke-slang-reflect
    tools/slang_reflect.cpp
)

target_include_directories(vke-slang-reflect
    PRIVATE
        "${VKE_SLANG_INCLUDE_DIR}"
)

target_link_libraries(vke-slang-reflect
    PRIVATE
        "${VKE_SLANG_LIBRARY}"
)

add_custom_command(TARGET vke-slang-reflect POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${VKE_SLANG_DLL}"
        "$<TARGET_FILE_DIR:vke-slang-reflect>"
    VERBATIM
)

vke_configure_target(vke-slang-reflect)

function(vke_add_slang_shader target)
    set(options)
    set(one_value_args SOURCE ENTRY STAGE OUTPUT PROFILE METADATA_OUTPUT REFLECTION_OUTPUT)
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

    if(VKE_SLANG_SHADER_REFLECTION_OUTPUT)
        list(APPEND shader_outputs "${VKE_SLANG_SHADER_REFLECTION_OUTPUT}")
        set(reflection_command
            COMMAND "$<TARGET_FILE:vke-slang-reflect>"
                --source "${VKE_SLANG_SHADER_SOURCE}"
                --entry "${VKE_SLANG_SHADER_ENTRY}"
                --stage "${VKE_SLANG_SHADER_STAGE}"
                --profile "${VKE_SLANG_SHADER_PROFILE}"
                --target spirv
                --output "${VKE_SLANG_SHADER_REFLECTION_OUTPUT}"
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
        ${reflection_command}
        DEPENDS
            "${VKE_SLANG_SHADER_SOURCE}"
            vke-slang-reflect
            ${VKE_SLANG_SHADER_DEPENDS}
        VERBATIM
    )

    add_custom_target("${target}" DEPENDS ${shader_outputs})
endfunction()

target_include_directories(vke-shader-slang
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(vke-shader-slang
    PUBLIC
        vke::core
)

vke_configure_target(vke-shader-slang)
