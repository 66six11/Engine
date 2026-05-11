cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineShaderSlang VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-shader-slang
  src/shader_reflection.cpp
)
add_library(asharia::shader_slang ALIAS asharia-shader-slang)

find_path(ASHARIA_SLANG_INCLUDE_DIR
    NAMES slang/slang.h
    HINTS
        "$ENV{VULKAN_SDK}/Include"
        "C:/VulkanSDK/1.4.321.1/Include"
    REQUIRED
)

find_library(ASHARIA_SLANG_LIBRARY
    NAMES slang slang.lib
    HINTS
        "$ENV{VULKAN_SDK}/Lib"
        "C:/VulkanSDK/1.4.321.1/Lib"
    REQUIRED
)

find_file(ASHARIA_SLANG_DLL
    NAMES slang.dll
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "C:/VulkanSDK/1.4.321.1/Bin"
    REQUIRED
)

find_program(ASHARIA_SLANGC_EXECUTABLE
    NAMES slangc slangc.exe
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "C:/VulkanSDK/1.4.321.1/Bin"
)

find_program(ASHARIA_SPIRV_VAL_EXECUTABLE
    NAMES spirv-val spirv-val.exe
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "C:/VulkanSDK/1.4.321.1/Bin"
)

add_executable(asharia-slang-reflect
    tools/slang_reflect.cpp
)

target_include_directories(asharia-slang-reflect
    PRIVATE
        "${ASHARIA_SLANG_INCLUDE_DIR}"
)

target_link_libraries(asharia-slang-reflect
    PRIVATE
        "${ASHARIA_SLANG_LIBRARY}"
)

add_custom_command(TARGET asharia-slang-reflect POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ASHARIA_SLANG_DLL}"
        "$<TARGET_FILE_DIR:asharia-slang-reflect>"
    VERBATIM
)

asharia_configure_target(asharia-slang-reflect)

function(asharia_add_slang_shader target)
    set(options)
    set(one_value_args SOURCE ENTRY STAGE OUTPUT PROFILE METADATA_OUTPUT REFLECTION_OUTPUT)
    set(multi_value_args DEPENDS)
    cmake_parse_arguments(ASHARIA_SLANG_SHADER
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    if(NOT ASHARIA_SLANG_SHADER_SOURCE)
        message(FATAL_ERROR "asharia_add_slang_shader requires SOURCE.")
    endif()
    if(NOT ASHARIA_SLANG_SHADER_ENTRY)
        message(FATAL_ERROR "asharia_add_slang_shader requires ENTRY.")
    endif()
    if(NOT ASHARIA_SLANG_SHADER_STAGE)
        message(FATAL_ERROR "asharia_add_slang_shader requires STAGE.")
    endif()
    if(NOT ASHARIA_SLANG_SHADER_OUTPUT)
        message(FATAL_ERROR "asharia_add_slang_shader requires OUTPUT.")
    endif()

    if(NOT ASHARIA_SLANG_SHADER_PROFILE)
        set(ASHARIA_SLANG_SHADER_PROFILE glsl_450)
    endif()

    if(NOT ASHARIA_SLANGC_EXECUTABLE)
        message(FATAL_ERROR "slangc is required to build Slang shaders.")
    endif()
    if(NOT ASHARIA_SPIRV_VAL_EXECUTABLE)
        message(FATAL_ERROR "spirv-val is required to validate Slang shader SPIR-V.")
    endif()

    get_filename_component(output_dir "${ASHARIA_SLANG_SHADER_OUTPUT}" DIRECTORY)
    set(shader_outputs "${ASHARIA_SLANG_SHADER_OUTPUT}")
    if(ASHARIA_SLANG_SHADER_METADATA_OUTPUT)
        list(APPEND shader_outputs "${ASHARIA_SLANG_SHADER_METADATA_OUTPUT}")
        set(metadata_command
            COMMAND "${CMAKE_COMMAND}"
                "-DASHARIA_SHADER_METADATA_OUTPUT=${ASHARIA_SLANG_SHADER_METADATA_OUTPUT}"
                "-DASHARIA_SHADER_SOURCE=${ASHARIA_SLANG_SHADER_SOURCE}"
                "-DASHARIA_SHADER_ENTRY=${ASHARIA_SLANG_SHADER_ENTRY}"
                "-DASHARIA_SHADER_STAGE=${ASHARIA_SLANG_SHADER_STAGE}"
                "-DASHARIA_SHADER_PROFILE=${ASHARIA_SLANG_SHADER_PROFILE}"
                "-DASHARIA_SHADER_TARGET=spirv"
                "-DASHARIA_SHADER_OUTPUT=${ASHARIA_SLANG_SHADER_OUTPUT}"
                "-DASHARIA_SLANGC_EXECUTABLE=${ASHARIA_SLANGC_EXECUTABLE}"
                "-DASHARIA_SPIRV_VAL_EXECUTABLE=${ASHARIA_SPIRV_VAL_EXECUTABLE}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmake/WriteShaderMetadata.cmake"
        )
    endif()

    if(ASHARIA_SLANG_SHADER_REFLECTION_OUTPUT)
        list(APPEND shader_outputs "${ASHARIA_SLANG_SHADER_REFLECTION_OUTPUT}")
        set(reflection_command
            COMMAND "$<TARGET_FILE:asharia-slang-reflect>"
                --source "${ASHARIA_SLANG_SHADER_SOURCE}"
                --entry "${ASHARIA_SLANG_SHADER_ENTRY}"
                --stage "${ASHARIA_SLANG_SHADER_STAGE}"
                --profile "${ASHARIA_SLANG_SHADER_PROFILE}"
                --target spirv
                --output "${ASHARIA_SLANG_SHADER_REFLECTION_OUTPUT}"
        )
    endif()

    add_custom_command(
        OUTPUT ${shader_outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
        COMMAND "${ASHARIA_SLANGC_EXECUTABLE}"
                "${ASHARIA_SLANG_SHADER_SOURCE}"
                -profile "${ASHARIA_SLANG_SHADER_PROFILE}"
                -target spirv
                -entry "${ASHARIA_SLANG_SHADER_ENTRY}"
                -stage "${ASHARIA_SLANG_SHADER_STAGE}"
                -o "${ASHARIA_SLANG_SHADER_OUTPUT}"
        COMMAND "${ASHARIA_SPIRV_VAL_EXECUTABLE}" "${ASHARIA_SLANG_SHADER_OUTPUT}"
        ${metadata_command}
        ${reflection_command}
        DEPENDS
            "${ASHARIA_SLANG_SHADER_SOURCE}"
            asharia-slang-reflect
            ${ASHARIA_SLANG_SHADER_DEPENDS}
        VERBATIM
    )

    add_custom_target("${target}" DEPENDS ${shader_outputs})
endfunction()

target_include_directories(asharia-shader-slang
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-shader-slang
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-shader-slang)
