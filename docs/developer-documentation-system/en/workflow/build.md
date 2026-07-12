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
The bootstrap script removes stale `ConanPresets.json` before install so generated presets reflect the current Conan profiles.
If any profile's `conan install` fails, bootstrap stops immediately and returns the same nonzero exit code; it does not continue to later profiles or report that toolchains are ready.

Current Conan requirements are `glfw/3.4`, `glm/1.0.1`, `imgui/1.92.7-docking`, `nlohmann_json/3.12.0`, `stb/cci.20240531`, `vulkan-headers/1.4.313.0`, and `vulkan-memory-allocator/3.3.0`.

## CMake Presets

Top-level presets currently define:

| Preset | Purpose | Tests |
|---|---|---|
| `msvc-debug` | Daily debug build | `ASHARIA_BUILD_TESTS=OFF` |
| `msvc-release` | MSVC release build | `ASHARIA_BUILD_TESTS=OFF` |
| `clangcl-debug` | Pre-commit debug build with clang-tidy integration enabled | `ASHARIA_BUILD_TESTS=OFF` |
| `clangcl-release` | ClangCL release check | `ASHARIA_BUILD_TESTS=OFF` |
| `msvc-debug-tests` | Complete native debug build and CTest gate | `ASHARIA_BUILD_TESTS=ON` |
| `clangcl-debug-tests` | Complete native debug build and CTest gate with clang-tidy | `ASHARIA_BUILD_TESTS=ON` |

Build and test presets use `jobs: 20`. Package-local test builds explicitly pass `-DASHARIA_BUILD_TESTS=ON`.

Top-level CMake options:

| Option | Default | Purpose |
|---|---|---|
| `ASHARIA_BUILD_APPS` | `ON` | Build `apps/editor` and `apps/sample-viewer` |
| `ASHARIA_BUILD_TESTS` | `OFF` | Enable package-local CTest targets |
| `ASHARIA_ENABLE_CLANG_TIDY` | `OFF` | Enable clang-tidy where supported; `clangcl-debug` turns it on |

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

## Top-Level Native Test Build

Bootstrap Conan first, then use the test-specific configure, build, and test presets. They use
`build/cmake/msvc-debug-tests` and `build/cmake/clangcl-debug-tests`, so enabling tests does not
mutate the normal application build trees.

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests --output-on-failure"
```

The ClangCL test preset enables clang-tidy for production and test translation units. Repository
clang-tidy diagnostics are warnings-as-errors; a reported diagnostic fails that build.

## Native Code Quality CI

`.github/workflows/native-code-quality.yml` runs on pull requests, pushes to `main`, and manual
dispatches. The job is pinned to the `windows-2022` runner because the committed Conan profiles
require Visual Studio 17; it must not follow `windows-latest` to a newer Visual Studio major version.
The Windows job installs the pinned Conan and Vulkan SDK toolchain, bootstraps Conan,
checks encoding, whitespace, and asset package boundaries, then builds and runs all registered
native CTests with both test presets. The hosted ClangCL build uses `--parallel 2` to bound the
combined memory peak of concurrent clang-tidy processes.

Hosted CI intentionally does not run GPU/window smoke commands because the runner is not the
repository's supported Vulkan presentation environment. The runtime smoke matrix in
`workflow/review.md` remains a local pre-commit requirement for changes in its scope.

## Package-Local Test Build

Use package-local configure when a package has `testTargets` in `asharia.package.json`:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/rendergraph -B build/cmake/package-rendergraph-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-rendergraph-tests-msvc-debug && ctest --test-dir build/cmake/package-rendergraph-tests-msvc-debug --output-on-failure"
```

## Studio Build

Avalonia Studio lives under `apps/studio` and uses .NET project files.
On Windows, Studio build/test expects the native output tree for `editor_native.dll` and `slang.dll` to exist under `build/cmake/<preset>`. Build the matching native CMake preset first, or pass `/p:StudioNativeBuildPreset=<preset>` to point Studio at an existing native output tree.

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
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
