# Workflow：构建

## Prerequisites

- Windows PowerShell 或 Developer PowerShell for VS 2022。
- Conan 2。
- CMake 3.28 或更新版本。
- Ninja。
- Visual Studio 2022 MSVC toolchain 和 clang-cl profile support。
- Vulkan SDK/loader。
- `packages/shader-slang` 需要 `slang/slang.h`、`slang.lib`、`slang.dll`、`slangc`、`spirv-val`。

## 目录约定

| 路径 | 说明 |
|---|---|
| `build/conan/` | Conan generated toolchains 和 dependency files |
| `build/cmake/` | CMake configure/build directories |
| `ConanPresets.json` | generated，按需删除/忽略 |
| `CMakeUserPresets.json` | 不能作为 committed presets 依赖 |

## Bootstrap Conan

CMake 前先运行：

```powershell
.\scripts\bootstrap-conan.ps1
```

它为 `windows-msvc-debug`、`windows-msvc-release`、`windows-clangcl-debug`、`windows-clangcl-release` 运行 `conan install`，并在存在 `conan.lock` 时使用 lockfile。

当前 Conan requirements：`glfw/3.4`、`glm/1.0.1`、`imgui/1.92.7-docking`、`nlohmann_json/3.12.0`、`stb/cci.20240531`、`vulkan-headers/1.4.313.0`、`vulkan-memory-allocator/3.3.0`。

## CMake Presets

| Preset | 用途 | Tests |
|---|---|---|
| `msvc-debug` | daily debug build | `ASHARIA_BUILD_TESTS=OFF` |
| `msvc-release` | MSVC release build | `ASHARIA_BUILD_TESTS=OFF` |
| `clangcl-debug` | pre-commit debug build，启用 clang-tidy integration | `ASHARIA_BUILD_TESTS=OFF` |
| `clangcl-release` | ClangCL release check | `ASHARIA_BUILD_TESTS=OFF` |

Build/test presets 使用 `jobs: 20`。Package-local tests 通过 `-DASHARIA_BUILD_TESTS=ON` 显式打开。

## Configure And Build

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-release\Release\generators\conanbuild.bat && cmake --preset msvc-release && cmake --build --preset msvc-release"
cmd /c "build\conan\clangcl-release\Release\generators\conanbuild.bat && cmake --preset clangcl-release && cmake --build --preset clangcl-release"
```

## Package-Local Test Build

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/rendergraph -B build/cmake/package-rendergraph-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-rendergraph-tests-msvc-debug && ctest --test-dir build/cmake/package-rendergraph-tests-msvc-debug --output-on-failure"
```

## Studio Build

Avalonia Studio 位于 `apps/studio`，使用 .NET project files。

```powershell
dotnet test apps\studio\Editor.sln
```

修改 `apps/studio` 时运行此命令。

## Common Failures

| Symptom | Likely cause | Recovery |
|---|---|---|
| CMake preset 找不到 toolchain | 未运行 Conan bootstrap 或删除了 `build/conan/` | 运行 `.\scripts\bootstrap-conan.ps1` |
| PowerShell 中 Ninja 找不到 MSVC compiler | Conan/VS 环境未激活 | 使用 `cmd /c "...conanbuild.bat && cmake ..."` |
| generated preset JSON parse error | JSON 带 BOM | 运行 encoding checker 并重新生成 |
| package standalone build 找不到 dependency target | 缺少 `asharia_require_package_target()` | 在 link 前添加 dependency package |

## Validation

Baseline：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```
