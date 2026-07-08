# Guide: Add A C++ Package

## Scope

Use this guide when a feature needs to become a reusable engine package instead of host-app internal implementation. Host-only UI, editor panels, and sample-only smoke code do not require a new package.

## Steps

### 1. Create Directories

```text
packages/<package-name>/
  CMakeLists.txt
  asharia.package.json
  include/asharia/<api_name>/
  src/
  tests/
```

`include/` is public API. Other packages must not include this package's `src/`.

### 2. Write Package Manifest

Example:

```json
{
  "name": "com.asharia.example",
  "version": "0.1.0",
  "displayName": "Asharia Example",
  "description": "Example reusable engine package.",
  "dependencies": [
    "com.asharia.core"
  ],
  "targets": [
    "asharia-example"
  ],
  "targetDependencies": [
    {
      "target": "asharia-example",
      "dependencies": [
        "asharia::core"
      ]
    }
  ],
  "testTargets": [
    "asharia-example-smoke-tests"
  ]
}
```

Manifest names document package-level dependency intent. `targets` and `targetDependencies` mirror the intended CMake shape for review, but actual target links still live in `CMakeLists.txt`.

### 3. Write CMake Target

Example:

```cmake
cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineExample VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::core engine/core)

add_library(asharia-example STATIC
    src/example.cpp
)
add_library(asharia::example ALIAS asharia-example)

target_include_directories(asharia-example
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(asharia-example
    PUBLIC
        asharia::core
)

asharia_configure_target(asharia-example)

if(ASHARIA_BUILD_TESTS)
  add_executable(asharia-example-smoke-tests
      tests/example_smoke_tests.cpp
  )
  target_link_libraries(asharia-example-smoke-tests PRIVATE asharia::example)
  asharia_configure_target(asharia-example-smoke-tests)
  add_test(NAME asharia-example-smoke-tests COMMAND asharia-example-smoke-tests)
endif()
```

### 4. Register Top-Level Build

Add the package to root `CMakeLists.txt` only when it should build as part of the full engine:

```cmake
add_subdirectory(packages/example)
```

### 5. Write Public API First

Public headers should include only:

- standard library headers,
- third-party public dependency headers that are part of the target contract,
- project headers from dependency targets.

Do not include another package's `src/`.

### 6. Add Tests

A package with public logic should have at least one package-local smoke/header test behind `ASHARIA_BUILD_TESTS`.

## Validation

Full build:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Package-local test:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/example -B build/cmake/package-example-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-example-tests-msvc-debug && ctest --test-dir build/cmake/package-example-tests-msvc-debug --output-on-failure"
```

Boundary checks:

```powershell
rg -n "#include .*src" packages\example engine packages
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```
