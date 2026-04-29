function(vke_json_escape output input)
    set(value "${input}")
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "" value "${value}")
    set("${output}" "${value}" PARENT_SCOPE)
endfunction()

foreach(required_var IN ITEMS
    VKE_SHADER_METADATA_OUTPUT
    VKE_SHADER_SOURCE
    VKE_SHADER_ENTRY
    VKE_SHADER_STAGE
    VKE_SHADER_PROFILE
    VKE_SHADER_TARGET
    VKE_SHADER_OUTPUT
    VKE_SLANGC_EXECUTABLE
    VKE_SPIRV_VAL_EXECUTABLE
)
    if(NOT DEFINED "${required_var}")
        message(FATAL_ERROR "${required_var} is required to write shader metadata.")
    endif()
endforeach()

if(NOT DEFINED VKE_SLANGC_VERSION OR NOT VKE_SLANGC_VERSION)
    execute_process(
        COMMAND "${VKE_SLANGC_EXECUTABLE}" -version
        OUTPUT_VARIABLE VKE_SLANGC_VERSION
        ERROR_VARIABLE VKE_SLANGC_VERSION_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT VKE_SLANGC_VERSION)
        set(VKE_SLANGC_VERSION "${VKE_SLANGC_VERSION_ERROR}")
    endif()
endif()

if(NOT DEFINED VKE_SPIRV_VAL_VERSION OR NOT VKE_SPIRV_VAL_VERSION)
    execute_process(
        COMMAND "${VKE_SPIRV_VAL_EXECUTABLE}" --version
        OUTPUT_VARIABLE VKE_SPIRV_VAL_VERSION
        ERROR_VARIABLE VKE_SPIRV_VAL_VERSION_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT VKE_SPIRV_VAL_VERSION)
        set(VKE_SPIRV_VAL_VERSION "${VKE_SPIRV_VAL_VERSION_ERROR}")
    endif()
endif()
string(REGEX REPLACE "\r?\n.*" "" VKE_SPIRV_VAL_VERSION "${VKE_SPIRV_VAL_VERSION}")

vke_json_escape(source "${VKE_SHADER_SOURCE}")
vke_json_escape(entry "${VKE_SHADER_ENTRY}")
vke_json_escape(stage "${VKE_SHADER_STAGE}")
vke_json_escape(profile "${VKE_SHADER_PROFILE}")
vke_json_escape(target "${VKE_SHADER_TARGET}")
vke_json_escape(output "${VKE_SHADER_OUTPUT}")
vke_json_escape(slangc_executable "${VKE_SLANGC_EXECUTABLE}")
vke_json_escape(slangc_version "${VKE_SLANGC_VERSION}")
vke_json_escape(spirv_val_executable "${VKE_SPIRV_VAL_EXECUTABLE}")
vke_json_escape(spirv_val_version "${VKE_SPIRV_VAL_VERSION}")

get_filename_component(metadata_dir "${VKE_SHADER_METADATA_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${metadata_dir}")
file(WRITE "${VKE_SHADER_METADATA_OUTPUT}" "{\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  \"source\": \"${source}\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  \"entry\": \"${entry}\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  \"stage\": \"${stage}\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  \"profile\": \"${profile}\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  \"target\": \"${target}\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  \"output\": \"${output}\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  \"compiler\": {\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "    \"name\": \"slangc\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "    \"path\": \"${slangc_executable}\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "    \"version\": \"${slangc_version}\"\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  },\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  \"validator\": {\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "    \"name\": \"spirv-val\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "    \"path\": \"${spirv_val_executable}\",\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "    \"version\": \"${spirv_val_version}\"\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "  }\n")
file(APPEND "${VKE_SHADER_METADATA_OUTPUT}" "}\n")
