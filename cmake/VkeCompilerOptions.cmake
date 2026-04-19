function(vke_configure_target target)
    get_target_property(target_type ${target} TYPE)

    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_features(${target} INTERFACE cxx_std_23)
        target_compile_options(${target} INTERFACE
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<CXX_COMPILER_ID:MSVC>:/permissive->
            $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>
        )
    else()
        target_compile_features(${target} PUBLIC cxx_std_23)

        set_target_properties(${target} PROPERTIES
            CXX_EXTENSIONS OFF
        )

        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<CXX_COMPILER_ID:MSVC>:/permissive->
            $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>
        )
    endif()
endfunction()
