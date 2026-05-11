function(asharia_json_escape output input)
    set(value "${input}")
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "" value "${value}")
    set("${output}" "${value}" PARENT_SCOPE)
endfunction()

foreach(required_var IN ITEMS
    ASHARIA_SHADER_METADATA_OUTPUT
    ASHARIA_SHADER_SOURCE
    ASHARIA_SHADER_ENTRY
    ASHARIA_SHADER_STAGE
    ASHARIA_SHADER_PROFILE
    ASHARIA_SHADER_TARGET
    ASHARIA_SHADER_OUTPUT
    ASHARIA_SLANGC_EXECUTABLE
    ASHARIA_SPIRV_VAL_EXECUTABLE
)
    if(NOT DEFINED "${required_var}")
        message(FATAL_ERROR "${required_var} is required to write shader metadata.")
    endif()
endforeach()

if(NOT DEFINED ASHARIA_SLANGC_VERSION OR NOT ASHARIA_SLANGC_VERSION)
    execute_process(
        COMMAND "${ASHARIA_SLANGC_EXECUTABLE}" -version
        OUTPUT_VARIABLE ASHARIA_SLANGC_VERSION
        ERROR_VARIABLE ASHARIA_SLANGC_VERSION_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT ASHARIA_SLANGC_VERSION)
        set(ASHARIA_SLANGC_VERSION "${ASHARIA_SLANGC_VERSION_ERROR}")
    endif()
endif()

if(NOT DEFINED ASHARIA_SPIRV_VAL_VERSION OR NOT ASHARIA_SPIRV_VAL_VERSION)
    execute_process(
        COMMAND "${ASHARIA_SPIRV_VAL_EXECUTABLE}" --version
        OUTPUT_VARIABLE ASHARIA_SPIRV_VAL_VERSION
        ERROR_VARIABLE ASHARIA_SPIRV_VAL_VERSION_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT ASHARIA_SPIRV_VAL_VERSION)
        set(ASHARIA_SPIRV_VAL_VERSION "${ASHARIA_SPIRV_VAL_VERSION_ERROR}")
    endif()
endif()
string(REGEX REPLACE "\r?\n.*" "" ASHARIA_SPIRV_VAL_VERSION "${ASHARIA_SPIRV_VAL_VERSION}")

asharia_json_escape(source "${ASHARIA_SHADER_SOURCE}")
asharia_json_escape(entry "${ASHARIA_SHADER_ENTRY}")
asharia_json_escape(stage "${ASHARIA_SHADER_STAGE}")
asharia_json_escape(profile "${ASHARIA_SHADER_PROFILE}")
asharia_json_escape(target "${ASHARIA_SHADER_TARGET}")
asharia_json_escape(output "${ASHARIA_SHADER_OUTPUT}")
asharia_json_escape(slangc_executable "${ASHARIA_SLANGC_EXECUTABLE}")
asharia_json_escape(slangc_version "${ASHARIA_SLANGC_VERSION}")
asharia_json_escape(spirv_val_executable "${ASHARIA_SPIRV_VAL_EXECUTABLE}")
asharia_json_escape(spirv_val_version "${ASHARIA_SPIRV_VAL_VERSION}")

get_filename_component(metadata_dir "${ASHARIA_SHADER_METADATA_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${metadata_dir}")
file(WRITE "${ASHARIA_SHADER_METADATA_OUTPUT}" "{\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  \"source\": \"${source}\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  \"entry\": \"${entry}\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  \"stage\": \"${stage}\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  \"profile\": \"${profile}\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  \"target\": \"${target}\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  \"output\": \"${output}\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  \"compiler\": {\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "    \"name\": \"slangc\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "    \"path\": \"${slangc_executable}\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "    \"version\": \"${slangc_version}\"\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  },\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  \"validator\": {\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "    \"name\": \"spirv-val\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "    \"path\": \"${spirv_val_executable}\",\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "    \"version\": \"${spirv_val_version}\"\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "  }\n")
file(APPEND "${ASHARIA_SHADER_METADATA_OUTPUT}" "}\n")
