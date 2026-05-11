include_guard(GLOBAL)

function(vke_package_init)
  if(NOT DEFINED VKE_REPOSITORY_ROOT)
    message(FATAL_ERROR "VKE_REPOSITORY_ROOT must be set before including VkePackage.cmake.")
  endif()

  get_filename_component(VKE_REPOSITORY_ROOT_ABSOLUTE "${VKE_REPOSITORY_ROOT}" ABSOLUTE)
  set(VKE_REPOSITORY_ROOT "${VKE_REPOSITORY_ROOT_ABSOLUTE}" CACHE PATH
      "Path to the VkEngine repository root." FORCE)
  set(VKE_REPOSITORY_ROOT "${VKE_REPOSITORY_ROOT_ABSOLUTE}" PARENT_SCOPE)

  if(NOT COMMAND vke_configure_target)
    include("${VKE_REPOSITORY_ROOT_ABSOLUTE}/cmake/VkeCompilerOptions.cmake")
  endif()

  if(NOT DEFINED VKE_BUILD_TESTS)
    option(VKE_BUILD_TESTS "Build VkEngine tests." OFF)
  endif()

  if(VKE_BUILD_TESTS)
    enable_testing()
  endif()
endfunction()

function(vke_require_package_target target package_relative_dir)
  if(TARGET ${target})
    return()
  endif()

  if(NOT DEFINED VKE_REPOSITORY_ROOT)
    message(FATAL_ERROR "VKE_REPOSITORY_ROOT must be set before requiring ${target}.")
  endif()

  get_filename_component(dependency_source_dir
      "${VKE_REPOSITORY_ROOT}/${package_relative_dir}" ABSOLUTE)
  if(NOT EXISTS "${dependency_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "Package dependency ${target} does not have a CMakeLists.txt at "
        "${dependency_source_dir}.")
  endif()

  string(MAKE_C_IDENTIFIER "${package_relative_dir}" dependency_build_name)
  add_subdirectory("${dependency_source_dir}"
      "${CMAKE_BINARY_DIR}/_vke_deps/${dependency_build_name}")

  if(NOT TARGET ${target})
    message(FATAL_ERROR
        "Package dependency ${target} was not defined by ${dependency_source_dir}.")
  endif()
endfunction()
