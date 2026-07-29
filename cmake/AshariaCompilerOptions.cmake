if(ASHARIA_MANAGES_CMAKE_CXX_CLANG_TIDY)
    unset(CMAKE_CXX_CLANG_TIDY CACHE)
    unset(ASHARIA_MANAGES_CMAKE_CXX_CLANG_TIDY CACHE)
endif()
if(DEFINED ASHARIA_ENABLE_CLANG_TIDY)
    unset(ASHARIA_ENABLE_CLANG_TIDY CACHE)
endif()

set(ASHARIA_WARNING_OPTIONS)
set(ASHARIA_CXX23_FALLBACK_OPTIONS)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    list(APPEND ASHARIA_WARNING_OPTIONS
        /EHsc
        /W4
        /Zc:__cplusplus
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        list(APPEND ASHARIA_WARNING_OPTIONS /permissive-)
    endif()

    list(APPEND ASHARIA_CXX23_FALLBACK_OPTIONS
        /std:c++latest
        /Zc:__cplusplus
    )
else()
    list(APPEND ASHARIA_WARNING_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
    )

    list(APPEND ASHARIA_CXX23_FALLBACK_OPTIONS
        -std=c++23
    )
endif()

function(asharia_require_cxx23 target scope)
    if(cxx_std_23 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
        target_compile_features(${target} ${scope} cxx_std_23)
    else()
        target_compile_options(${target} ${scope} ${ASHARIA_CXX23_FALLBACK_OPTIONS})
    endif()
endfunction()

function(asharia_configure_target target)
    get_target_property(target_type ${target} TYPE)

    if(target_type STREQUAL "INTERFACE_LIBRARY")
        asharia_require_cxx23(${target} INTERFACE)
        target_compile_options(${target} INTERFACE ${ASHARIA_WARNING_OPTIONS})
    else()
        asharia_require_cxx23(${target} PUBLIC)

        set_target_properties(${target} PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )

        target_compile_options(${target} PRIVATE ${ASHARIA_WARNING_OPTIONS})
    endif()
endfunction()
