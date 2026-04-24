if(VKE_ENABLE_CLANG_TIDY)
    find_program(VKE_CLANG_TIDY_EXECUTABLE NAMES clang-tidy clang-tidy.exe REQUIRED)

    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set(CMAKE_CXX_CLANG_TIDY
            "${VKE_CLANG_TIDY_EXECUTABLE}"
            "--extra-arg-before=/EHsc"
            "--extra-arg=-fexceptions"
            "--extra-arg=-fcxx-exceptions"
            CACHE STRING "clang-tidy executable used for C++ linting." FORCE
        )
        set(VKE_MANAGES_CMAKE_CXX_CLANG_TIDY ON CACHE INTERNAL "VkEngine manages CMAKE_CXX_CLANG_TIDY.")
        message(STATUS "VkEngine clang-tidy build integration enabled: ${VKE_CLANG_TIDY_EXECUTABLE}")
    else()
        if(VKE_MANAGES_CMAKE_CXX_CLANG_TIDY)
            unset(CMAKE_CXX_CLANG_TIDY CACHE)
            unset(VKE_MANAGES_CMAKE_CXX_CLANG_TIDY CACHE)
        endif()
        message(STATUS
            "VKE_ENABLE_CLANG_TIDY is ON. Visual Studio clang-tidy analysis is expected to come from the "
            "configure preset vendor settings when using generator '${CMAKE_GENERATOR}'."
        )
    endif()
elseif(VKE_MANAGES_CMAKE_CXX_CLANG_TIDY)
    unset(CMAKE_CXX_CLANG_TIDY CACHE)
    unset(VKE_MANAGES_CMAKE_CXX_CLANG_TIDY CACHE)
endif()

set(VKE_WARNING_OPTIONS)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    list(APPEND VKE_WARNING_OPTIONS
        /EHsc
        /W4
        /Zc:__cplusplus
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        list(APPEND VKE_WARNING_OPTIONS /permissive-)
    endif()
else()
    list(APPEND VKE_WARNING_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

function(vke_configure_target target)
    get_target_property(target_type ${target} TYPE)

    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_features(${target} INTERFACE cxx_std_23)
        target_compile_options(${target} INTERFACE ${VKE_WARNING_OPTIONS})
    else()
        target_compile_features(${target} PUBLIC cxx_std_23)

        set_target_properties(${target} PROPERTIES
            CXX_EXTENSIONS OFF
        )

        target_compile_options(${target} PRIVATE ${VKE_WARNING_OPTIONS})
    endif()
endfunction()
