include_guard(GLOBAL)

function(asharia_package_init)
  if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
    message(FATAL_ERROR "ASHARIA_REPOSITORY_ROOT must be set before including AshariaPackage.cmake.")
  endif()

  get_filename_component(ASHARIA_REPOSITORY_ROOT_ABSOLUTE "${ASHARIA_REPOSITORY_ROOT}" ABSOLUTE)
  set(ASHARIA_REPOSITORY_ROOT "${ASHARIA_REPOSITORY_ROOT_ABSOLUTE}" CACHE PATH
      "Path to the Asharia Engine repository root." FORCE)
  set(ASHARIA_REPOSITORY_ROOT "${ASHARIA_REPOSITORY_ROOT_ABSOLUTE}" PARENT_SCOPE)

  if(NOT COMMAND asharia_configure_target)
    include("${ASHARIA_REPOSITORY_ROOT_ABSOLUTE}/cmake/AshariaCompilerOptions.cmake")
  endif()

  if(NOT DEFINED ASHARIA_BUILD_TESTS)
    option(ASHARIA_BUILD_TESTS "Build Asharia Engine tests." OFF)
  endif()

  if(ASHARIA_BUILD_TESTS)
    enable_testing()
  endif()
endfunction()

function(asharia_require_package_target target package_relative_dir)
  if(TARGET ${target})
    return()
  endif()

  if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
    message(FATAL_ERROR "ASHARIA_REPOSITORY_ROOT must be set before requiring ${target}.")
  endif()

  get_filename_component(dependency_source_dir
      "${ASHARIA_REPOSITORY_ROOT}/${package_relative_dir}" ABSOLUTE)
  if(NOT EXISTS "${dependency_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR
        "Package dependency ${target} does not have a CMakeLists.txt at "
        "${dependency_source_dir}.")
  endif()

  string(MAKE_C_IDENTIFIER "${package_relative_dir}" dependency_build_name)
  add_subdirectory("${dependency_source_dir}"
      "${CMAKE_BINARY_DIR}/_asharia_deps/${dependency_build_name}")

  if(NOT TARGET ${target})
    message(FATAL_ERROR
        "Package dependency ${target} was not defined by ${dependency_source_dir}.")
  endif()
endfunction()
