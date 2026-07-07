# Workflow: Build

## Prerequisites

- Windows PowerShell or Developer PowerShell for VS 2022.
- Conan 2.
- CMake 3.28 or newer.
- Ninja.
- Visual Studio 2022 MSVC toolchain and clang-cl profile support.
- Vulkan SDK/loader availability as required by Conan/Vulkan packages.
- Slang build/runtime files required by `packages/shader-slang`: `slang/slang.h`, `slang.lib`, `slang.dll`, `slangc`, and `spirv-val`.

## Directory Conventions

| Path | Description |
|---|---|
| `build/conan/` | Conan generated toolchains and dependency files |
| `build/cmake/` | CMake configure/build directories |
| `ConanPresets.json` | Generated and removed/ignored as needed |
| `CMakeUserPresets.json` | Must not be relied on for committed presets |

`build/conan/` and `build/cmake/` are separate because Visual Studio may delete CMake cache directories without regenerating Conan toolchains.

## Bootstrap Conan

Run before CMake:

```powershell
.\scripts\bootstrap-conan.ps1
```

This runs `conan install` for:

- `windows-msvc-debug`,
- `windows-msvc-release`,
- `windows-clangcl-debug`,
- `windows-clangcl-release`.

It uses `conan.lock` when present.

Current Conan requirements are `glfw/3.4`, `glm/1.0.1`, `imgui/1.92.7-docking`, `nlohmann_json/3.12.0`, `stb/cci.20240531`, `vulkan-headers/1.4.313.0`, and `vulkan-memory-allocator/3.3.0`.

## CMake Presets

Top-level presets currently define:

| Preset | Purpose | Tests |
|---|---|---|
| `msvc-debug` | Daily debug build | `ASHARIA_BUILD_TESTS=OFF` |
| `msvc-release` | MSVC release build | `ASHARIA_BUILD_TESTS=OFF` |
| `clangcl-debug` | Pre-commit debug build with clang-tidy integration enabled | `ASHARIA_BUILD_TESTS=OFF` |
| `clangcl-release` | ClangCL release check | `ASHARIA_BUILD_TESTS=OFF` |

Build and test presets use `jobs: 20`. Package-local test builds explicitly pass `-DASHARIA_BUILD_TESTS=ON`.

## Configure And Build

MSVC debug:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

ClangCL debug with clang-tidy:

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
```

Release examples:

```powershell
cmd /c "build\conan\msvc-release\Release\generators\conanbuild.bat && cmake --preset msvc-release && cmake --build --preset msvc-release"
cmd /c "build\conan\clangcl-release\Release\generators\conanbuild.bat && cmake --preset clangcl-release && cmake --build --preset clangcl-release"
```

## Package-Local Test Build

Use package-local configure when a package has `testTargets` in `asharia.package.json`:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/rendergraph -B build/cmake/package-rendergraph-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-rendergraph-tests-msvc-debug && ctest --test-dir build/cmake/package-rendergraph-tests-msvc-debug --output-on-failure"
```

## Studio Build

Avalonia Studio lives under `apps/studio` and uses .NET project files.

```powershell
dotnet test apps\studio\Editor.sln
```

Run this when editing `apps/studio`.

## Common Failures

| Symptom | Likely cause | Recovery |
|---|---|---|
| CMake preset cannot find toolchain | Conan bootstrap was not run or `build/conan/` was deleted | Run `.\scripts\bootstrap-conan.ps1` |
| Ninja cannot find MSVC compiler from PowerShell | Conan/VS environment not active | Use the `cmd /c "...conanbuild.bat && cmake ..."` form |
| JSON parse error in generated presets | BOM in generated JSON file | Run encoding checker and regenerate Conan files |
| Package standalone build cannot find dependency target | Missing `asharia_require_package_target()` call | Add dependency package before target link |

## Validation

Baseline:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```
