# Guide：新增 C++ Package

## 适用范围

当功能需要成为可复用 engine package，而不是 host app 内部实现时，使用本指南。Host-only UI、editor panel、sample-only smoke 不需要新 package。

## 步骤

### 1. 建目录

```text
packages/<package-name>/
  CMakeLists.txt
  asharia.package.json
  include/asharia/<api_name>/
  src/
  tests/
```

`include/` 是 public API。`src/` 不能被其他 package include。

### 2. 写 package manifest

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

Manifest 记录 package-level dependency intent。`targets` 和 `targetDependencies` 用于 review CMake 形态；真实 target links 仍写在 `CMakeLists.txt`。

### 3. 写 CMake target

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

target_include_directories(asharia-example PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(asharia-example PUBLIC asharia::core)
asharia_configure_target(asharia-example)
```

### 4. 注册 top-level build

需要进入全引擎构建时，在 root `CMakeLists.txt` 添加：

```cmake
add_subdirectory(packages/example)
```

### 5. 写 public API

Public headers 只能 include 标准库、公开 third-party dependency headers、依赖 target 的 project headers。不要 include 其他 package 的 `src/`。

### 6. 加测试

有 public logic 的 package 至少应有一个 `ASHARIA_BUILD_TESTS` 下的 package-local smoke/header test。

## 验证方式

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/example -B build/cmake/package-example-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-example-tests-msvc-debug && ctest --test-dir build/cmake/package-example-tests-msvc-debug --output-on-failure"
rg -n "#include .*src" packages\example engine packages
```
