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
Bootstrap script 会在 install 前删除 stale `ConanPresets.json`，确保 generated presets 与当前 Conan profiles 一致。
任一 profile 的 `conan install` 失败时，bootstrap 会立即停止并返回同一个非零退出码，不会继续后续 profile 或报告 toolchain 已就绪。

当前 Conan requirements：`glfw/3.4`、`glm/1.0.1`、`imgui/1.92.7-docking`、`nlohmann_json/3.12.0`、`stb/cci.20240531`、`vulkan-headers/1.4.313.0`、`vulkan-memory-allocator/3.3.0`。

## CMake Presets

| Preset | 用途 | Tests |
|---|---|---|
| `msvc-debug` | daily debug build | `ASHARIA_BUILD_TESTS=OFF` |
| `msvc-release` | MSVC release build | `ASHARIA_BUILD_TESTS=OFF` |
| `clangcl-debug` | pre-commit debug build，启用 clang-tidy integration | `ASHARIA_BUILD_TESTS=OFF` |
| `clangcl-release` | ClangCL release check | `ASHARIA_BUILD_TESTS=OFF` |
| `msvc-debug-tests` | native Debug build + CTest gate | `ASHARIA_BUILD_TESTS=ON` |
| `clangcl-debug-tests` | native Debug build + CTest + clang-tidy gate | `ASHARIA_BUILD_TESTS=ON` |

Build/test presets 使用 `jobs: 20`。Package-local tests 通过 `-DASHARIA_BUILD_TESTS=ON` 显式打开。

Top-level CMake options：

| Option | Default | 用途 |
|---|---|---|
| `ASHARIA_BUILD_APPS` | `ON` | 构建 `apps/editor` 和 `apps/sample-viewer` |
| `ASHARIA_BUILD_TESTS` | `OFF` | 启用 package-local CTest targets |
| `ASHARIA_ENABLE_CLANG_TIDY` | `OFF` | 启用支持 target 的 clang-tidy；`clangcl-debug` 会打开它 |

## Configure And Build

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-release\Release\generators\conanbuild.bat && cmake --preset msvc-release && cmake --build --preset msvc-release"
cmd /c "build\conan\clangcl-release\Release\generators\conanbuild.bat && cmake --preset clangcl-release && cmake --build --preset clangcl-release"
```

## Top-Level Native Test Build

CMake 之前必须先 bootstrap Conan。Test configure/build/test presets 使用独立目录
`build/cmake/msvc-debug-tests` 和 `build/cmake/clangcl-debug-tests`，不会改写日常 application build tree。

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests --output-on-failure"
```

ClangCL test preset 对 production 和 test translation units 启用 clang-tidy。仓库把所有 clang-tidy
diagnostics 作为 errors，任何未处理 diagnostic 都会使构建失败。

## Native Code Quality CI

`.github/workflows/native-code-quality.yml` 在 pull request、push to `main` 和 manual dispatch 时运行。
Job 固定使用包含 Visual Studio 2022 的 `windows-2022` runner，因为 committed Conan profiles 要求
Visual Studio 17；不能让 `windows-latest` 自动迁移到更新的 Visual Studio major version。
Windows job 安装锁定版本的 Conan 和 Vulkan SDK，先 bootstrap Conan，然后检查 encoding、whitespace 和
asset package boundaries，最后使用两个 test preset 构建并运行所有 native CTests。Hosted ClangCL build
使用 `--parallel 2`，限制多个 clang-tidy process 的总内存峰值。

Hosted CI 不运行 GPU/window smokes，因为 hosted runner 不是本仓库支持的 Vulkan presentation 环境。
`workflow/review.md` 中的 runtime smoke matrix 仍是相关改动的本地 pre-commit gate。

## Package-Local Test Build

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/rendergraph -B build/cmake/package-rendergraph-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-rendergraph-tests-msvc-debug && ctest --test-dir build/cmake/package-rendergraph-tests-msvc-debug --output-on-failure"
```

## Studio Build

Avalonia Studio 位于 `apps/studio`，使用 .NET project files。
Windows 上 build/test Studio 前，需要先有包含 `editor_native.dll` 和 `slang.dll` 的 native output tree，默认位于 `build/cmake/<preset>`。先构建匹配的 native CMake preset，或传入 `/p:StudioNativeBuildPreset=<preset>` 指向已有 native 输出。

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
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
