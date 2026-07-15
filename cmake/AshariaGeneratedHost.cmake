include_guard(GLOBAL)

set(ASHARIA_GENERATED_HOST_TEMPLATE_ROOT "" CACHE PATH
    "Explicit immutable generated Host Template root to include.")

function(asharia_include_generated_host_template)
  if(ASHARIA_GENERATED_HOST_TEMPLATE_ROOT STREQUAL "")
    return()
  endif()

  get_filename_component(_asharia_host_template_root
      "${ASHARIA_GENERATED_HOST_TEMPLATE_ROOT}" ABSOLUTE)
  set(_asharia_host_template_fragment
      "${_asharia_host_template_root}/asharia-host-template.cmake")
  if(NOT EXISTS "${_asharia_host_template_fragment}")
    message(FATAL_ERROR
        "ASHARIA_GENERATED_HOST_TEMPLATE_ROOT does not contain "
        "asharia-host-template.cmake: ${_asharia_host_template_root}")
  endif()

  include("${_asharia_host_template_fragment}")
endfunction()
