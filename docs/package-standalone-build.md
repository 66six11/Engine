# Standalone Package Builds

VkEngine packages can be configured from their own directories while still
living in the monorepo. This is the first step toward future split-repository
packages: package CMake files no longer require the repository root
`CMakeLists.txt` to be the entry point.

## Scope

This mode is a development convenience, not a binary SDK or published package
format yet.

- A package can be used as the CMake source directory.
- Missing local dependencies are added under the build tree as `_vke_deps`.
- Conan still owns external dependencies and toolchain generation.
- The repository root is still the source of shared CMake helpers and local
  package dependencies.
- `vke.package.json` records package targets plus `targetDependencies` so
  package-level dependencies do not have to blur target-level boundaries.

## Example

Run Conan bootstrap first if `build/conan/` is missing:

```powershell
.\scripts\bootstrap-conan.ps1
```

Configure and build one package from ordinary PowerShell:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\rendergraph -B build\cmake\package-rendergraph-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=D:/TechArt/VkEngine/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-rendergraph-msvc-debug"
```

High-level packages work the same way:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\renderer-basic -B build\cmake\package-renderer-basic-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=D:/TechArt/VkEngine/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-renderer-basic-msvc-debug"
```

Apps can also be configured directly when their package dependencies are
available through the monorepo:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S apps\sample-viewer -B build\cmake\package-sample-viewer-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=D:/TechArt/VkEngine/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-sample-viewer-msvc-debug"
```

## Repository Root Override

Each package assumes the repository root is two directories above its
`CMakeLists.txt`. If a future superbuild checks a package out elsewhere, pass
`VKE_REPOSITORY_ROOT` to point at a workspace that contains VkEngine's shared
`cmake/`, `engine/`, `packages/`, and `apps/` directories:

```powershell
cmake -S packages\rendergraph -B build\cmake\package-rendergraph-msvc-debug -DVKE_REPOSITORY_ROOT=D:/TechArt/VkEngine
```

## Package Tests

Pure CPU packages can expose package-local CTest entries. Reflection and
serialization currently have standalone smoke tests:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\reflection -B build\cmake\package-reflection-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVKE_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=D:/TechArt/VkEngine/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-reflection-tests-msvc-debug && ctest --test-dir build\cmake\package-reflection-tests-msvc-debug --output-on-failure"
```

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\serialization -B build\cmake\package-serialization-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVKE_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=D:/TechArt/VkEngine/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-serialization-tests-msvc-debug && ctest --test-dir build\cmake\package-serialization-tests-msvc-debug --output-on-failure"
```

Package manifests may list these under `testTargets` while keeping shipping
targets in `targets`.

## Current Limitations

- This does not install or export CMake packages.
- This does not create per-package Conan recipes.
- This does not make a stable C++ ABI or DLL boundary.
- Vulkan/runtime smoke coverage is still routed through `apps/sample-viewer`
  flags until those packages grow their own focused test targets.
