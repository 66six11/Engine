function(vke_configure_target target)
    target_compile_features(${target} INTERFACE cxx_std_23)

    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
    )

    if(MSVC)
        target_compile_options(${target} INTERFACE
            /W4
            /permissive-
            /Zc:__cplusplus
        )
    else()
        target_compile_options(${target} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()
