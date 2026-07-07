# Architecture: Overall Layers

## Purpose

This document describes the current system layers, state ownership, and dependency direction in the codebase. It does not describe the future roadmap. Future work is labeled with `future`.

## Companion Documents

Read these architecture documents for deeper boundaries:

- [package-dependency-map.md](package-dependency-map.md): package and target dependency matrix.
- [data-model-and-persistence.md](data-model-and-persistence.md): archive, schema, persistence, reflection, serialization, asset, material, and shader data ownership.
- [rendering-and-frame-flow.md](rendering-and-frame-flow.md): frame loop, RenderGraph, RHI, renderer, and Vulkan flow.
- [asset-and-material-flow.md](asset-and-material-flow.md): asset product and material/shader flow.
- [editor-runtime-boundaries.md](editor-runtime-boundaries.md): C++ editor, native bridge, Studio, and runtime ownership.

## Top-Level Code Structure

| Path | Current responsibility | Build entry |
|---|---|---|
| `engine/core` | Base types such as `Error`, `Result`, logging, and version data | `asharia::core` |
| `engine/platform` | Platform abstraction interface layer, currently an interface target | `asharia::platform` |
| `packages/*` | Independently buildable engine packages | `asharia::<name>` alias |
| `apps/sample-viewer` | Interactive sample viewer and runtime smoke entry point | `asharia-sample-viewer` |
| `apps/editor` | C++ ImGui editor host, native bridge, and editor smoke entry points | `asharia-editor`, `editor-native` |
| `apps/studio` | Avalonia Studio shell and .NET tests | `apps/studio/Editor.sln` |
| `tools/asset-processor` | Asset pipeline CLI and smoke entry points | `asharia-asset-processor` |
| `cmake` | Package-first helpers and compiler options | `AshariaPackage.cmake` |

## Package Boundaries

Each C++ package defines its target in its own `CMakeLists.txt` and exposes it to other packages through an `asharia::<name>` alias. Cross-package dependencies use `asharia_require_package_target(target package_relative_dir)`. In standalone package builds, that helper adds dependency packages under the `_asharia_deps` build directory.

Current rules:

- Public API is exposed only from a package's `include/`; `src/` is implementation detail.
- Callers depend on CMake targets and do not include another package's `src/`.
- `asharia_configure_target()` applies C++23 and warning options consistently to static, shared, executable, and interface targets.
- When `ASHARIA_BUILD_TESTS` is ON, package-local smoke and header tests are added to the build.

## State Ownership

| State | Owner | Public access |
|---|---|---|
| Errors and return values | `engine/core` | `asharia::Error`, `Result<T>`, `VoidResult` |
| JSON-like archive value | Archive value types owned separately by `packages/archive` and `packages/serialization` | package API |
| schema registry | `packages/schema` | `asharia::schema::SchemaRegistry` |
| runtime reflection registry | `packages/reflection` | `asharia::reflection::TypeRegistry` |
| scene world | `packages/scene-core` | `asharia::scene::World` |
| asset identity and catalog metadata | `packages/asset-core` | `AssetGuid`, `SourceAssetRecord`, `AssetCatalog` |
| asset import planning and execution | `packages/asset-pipeline` | `planAssetImports()`, `executeAssetProducts()` |
| render graph declarations and compile result | `packages/rendergraph` | `RenderGraph`, `RenderGraphCompileResult` |
| Vulkan instance, device, swapchain, and resources | `packages/rhi-vulkan` | RAII classes such as `VulkanContext` and `VulkanFrameLoop` |
| builtin renderer schemas and Vulkan recorders | `packages/renderer-basic` | `renderer_basic` schema layer and `renderer_basic_vulkan` implementation layer |
| editor panel, action, and tool state | `apps/editor` or `apps/studio`, depending on host | editor-host APIs, not engine package APIs |

## Dependency Direction

Current dependency facts from package manifests and CMake:

- `engine/core` has no package dependency.
- `engine/platform` depends on `asharia::core`.
- `packages/rendergraph` depends on `asharia::core`.
- `packages/rhi-vulkan` static target links `asharia::core`, Vulkan, Vulkan headers, and VMA. Its separate interface target `asharia::rhi_vulkan_rendergraph` links `asharia::rhi_vulkan` and `asharia::rendergraph`.
- `packages/renderer-basic` splits backend-agnostic `asharia::renderer_basic` from Vulkan implementation `asharia::renderer_basic_vulkan`.
- `apps/sample-viewer` and `apps/editor` may aggregate runtime packages because they are host applications.

Hard constraints:

- `asharia::rendergraph` must remain backend-agnostic. It cannot include Vulkan headers.
- Vulkan layout, stage, access, barrier, swapchain, and command buffer details belong to `asharia::rhi_vulkan` or `asharia::rhi_vulkan_rendergraph`.
- `asharia::renderer_basic` defines high-level renderer schemas and shared data. Vulkan command recording belongs to `asharia::renderer_basic_vulkan`.
- Runtime packages must not depend on `apps/editor` or `apps/studio`.
- `apps/studio` talks to native rendering through interop models and native bridge APIs; it must not become a C++ package dependency of renderer packages.

## Lifetime

1. Conan generates toolchains under `build/conan/<profile>/<config>/generators/`.
2. CMake configure uses presets and generated Conan toolchain files.
3. Package targets are configured with C++23 and warning settings.
4. The host app creates platform and window state.
5. The Vulkan host creates `VulkanContext`, then `VulkanFrameLoop`.
6. Renderer code declares a `RenderGraph`, compiles it, maps graph state to backend state, records commands, and presents or publishes output.
7. RAII owners destroy Vulkan objects in reverse ownership order. Frame-loop code uses deferred deletion for resources that cannot be released immediately.

## Extension Points

Current extension points:

- New runtime package: add `packages/<name>/CMakeLists.txt`, `asharia.package.json`, public headers, implementation, and tests.
- New RenderGraph pass type: add schema in a backend-agnostic layer, then add executor or recorder code in a backend implementation.
- New asset importer: extend asset pipeline planning and execution, then add tool smoke coverage.
- New editor panel or tool: add editor host registration and tests in the host that owns the UI.

`future`: a generated package dependency graph can be added after manifests become the single source for both CMake and documentation. Until then, docs must name both CMake targets and manifest packages when a dependency matters.

## Validation

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Checkpoints:

- `asharia::rhi_vulkan` target does not publicly link `asharia::rendergraph`.
- `asharia::rhi_vulkan_rendergraph` is the only target in `packages/rhi-vulkan` that exposes RenderGraph integration headers.
- `asharia::renderer_basic` and `asharia::renderer_basic_vulkan` remain separate targets.
