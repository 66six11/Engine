# Architecture: Package Dependency Map

## Purpose

This document maps the current package and target dependency structure. It answers which layer owns each responsibility, which targets can depend on which other targets, and where target-level exceptions live.

It records current code facts. Future automation is labeled with `future`.

## Layers

| Layer | Current paths | Dependency direction |
|---|---|---|
| Foundation | `engine/core`, `engine/platform`, `packages/profiling` | Other layers may depend on foundation. Foundation must not depend on renderer, assets, editor, or tools. |
| Data contracts | `packages/archive`, `packages/schema`, `packages/cpp-binding`, `packages/persistence`, `packages/reflection`, `packages/serialization` | May depend on foundation and their declared data-contract prerequisites. Must not depend on renderer or editor hosts. |
| Asset and material model | `packages/asset-core`, `packages/project-core`, `packages/resource-runtime`, `packages/material-core`, `packages/material-instance`, `packages/shader-authoring`, `packages/shader-slang`, `packages/shader-material-adapter`, `packages/asset-pipeline` | May depend on foundation and data contracts. Asset/model packages do not depend on `apps/*`. |
| Runtime rendering | `packages/rendergraph`, `packages/rhi-vulkan`, `packages/renderer-basic`, `packages/window-glfw`, `packages/scene-core` | Rendering packages depend downward into foundation/model packages. Vulkan integration targets own Vulkan-specific translation. |
| Hosts and tools | `apps/sample-viewer`, `apps/editor`, `apps/studio`, `tools/asset-processor` | Hosts aggregate packages. Runtime packages must not depend back on hosts. |

## Current Package Matrix

| Path | Main target or build entry | Current package dependencies | Current owner |
|---|---|---|---|
| `engine/core` | `asharia::core` | none | base errors, results, logging, version data |
| `engine/platform` | `asharia::platform` | `com.asharia.core` | platform abstraction interface |
| `packages/profiling` | `asharia::profiling` | none | lightweight profiling helpers |
| `packages/archive` | `asharia::archive` | `com.asharia.core` | JSON-like archive value and JSON IO |
| `packages/schema` | `asharia::schema` | `com.asharia.core` | schema documents and registry |
| `packages/cpp-binding` | `asharia::cpp_binding` | `com.asharia.core`, `com.asharia.schema` | C++ type binding metadata over schema |
| `packages/persistence` | `asharia::persistence` | `com.asharia.core`, `com.asharia.schema`, `com.asharia.archive`, `com.asharia.cpp-binding` | archive-backed persistence and migration |
| `packages/reflection` | `asharia::reflection` | `com.asharia.core` | runtime type registry |
| `packages/serialization` | `asharia::serialization` | `com.asharia.core`, `com.asharia.reflection` | reflection-based serialization |
| `packages/asset-core` | `asharia::asset_core`, `asharia::asset_core_io` | `com.asharia.core`, `com.asharia.archive` | asset identity, metadata, catalog, metadata IO |
| `packages/project-core` | `asharia::project_core`, `asharia::project_core_io` | `com.asharia.core`, `com.asharia.archive` | project descriptor and project IO |
| `packages/resource-runtime` | `asharia::resource_runtime` | `com.asharia.asset-core` | runtime resource handles, tickets, and product record resolution |
| `packages/material-core` | `asharia::material_core` | `com.asharia.core` | material pipeline keys and resource signatures |
| `packages/material-instance` | `asharia::material_instance` | `com.asharia.core`, `com.asharia.archive`, `com.asharia.asset-core`, `com.asharia.shader-authoring` | `.amat` document IO and resolution |
| `packages/shader-authoring` | `asharia::shader_authoring` | `com.asharia.core` | `.ashader` document model, parser, generated Slang |
| `packages/shader-slang` | `asharia::shader_slang`, `asharia-slang-reflect` | `com.asharia.core` | Slang compilation helpers and reflection model |
| `packages/shader-material-adapter` | `asharia::shader_material_adapter` | `com.asharia.core`, `com.asharia.material-core`, `com.asharia.shader-authoring`, `com.asharia.shader-slang` | reflection to material-signature translation |
| `packages/asset-pipeline` | `asharia::asset_pipeline` | `com.asharia.archive`, `com.asharia.asset-core`, `com.asharia.material-instance`, `com.asharia.shader-authoring` | source discovery, import planning, product execution |
| `packages/rendergraph` | `asharia::rendergraph` | `com.asharia.core` | backend-agnostic graph declarations, compile result, diagnostics |
| `packages/rhi-vulkan` | `asharia::rhi_vulkan`, `asharia::rhi_vulkan_rendergraph` | `com.asharia.core`, `com.asharia.rendergraph` | Vulkan backend and the separate RenderGraph-to-Vulkan bridge |
| `packages/renderer-basic` | `asharia::renderer_basic`, `asharia::renderer_basic_vulkan` | `com.asharia.core`, `com.asharia.material-core`, `com.asharia.rendergraph`, `com.asharia.rhi-vulkan`, `com.asharia.shader-slang` | backend-agnostic renderer schemas and Vulkan recorders |
| `packages/window-glfw` | `asharia::window_glfw` | `com.asharia.core`, `com.asharia.platform` | GLFW window integration |
| `packages/scene-core` | `asharia::scene_core` | `com.asharia.core` | scene world, entity IDs, names, transforms |
| `apps/sample-viewer` | `asharia-sample-viewer` | runtime aggregate | sample host and runtime smoke entry point |
| `apps/editor` | `asharia-editor`, `editor-native` | editor/runtime aggregate | C++ ImGui editor host, native bridge, editor smokes |
| `apps/studio` | `apps/studio/Editor.sln` | managed project references | Avalonia Studio shell, managed models, .NET tests |
| `tools/asset-processor` | `asharia-asset-processor` | asset tool aggregate | asset pipeline CLI |

## Target-Level Exceptions

The manifest dependency list names package-level facts. CMake target dependencies are stricter and are the source of truth for boundary checks.

- `packages/rhi-vulkan` has a package-level dependency on `com.asharia.rendergraph` because the package contains `asharia::rhi_vulkan_rendergraph`. The base `asharia::rhi_vulkan` target links `asharia::core` and Vulkan dependencies only.
- `packages/renderer-basic` contains both `asharia::renderer_basic` and `asharia::renderer_basic_vulkan`. The backend-agnostic `asharia::renderer_basic` target links `asharia::core`, `asharia::rendergraph`, and `asharia::shader_slang`; Vulkan command recording is isolated in `asharia::renderer_basic_vulkan`.
- `packages/asset-core` and `packages/project-core` split model targets from IO targets. The model target stays smaller; the IO target brings in `asharia::archive`.
- `packages/shader-material-adapter` test targets may use `shader-authoring` fixtures, but the adapter library target links only `asharia::core`, `asharia::material_core`, and `asharia::shader_slang`.
- `apps/editor` builds a shared `editor-native` bridge for Studio interop and the `asharia-editor` executable for the native ImGui host.

## State Ownership By Layer

| State | Owner | May observe | Must not mutate |
|---|---|---|---|
| Error category and return status | `engine/core` | all packages | host apps must not invent incompatible error carriers for public package APIs |
| Schema definitions | `packages/schema` | `cpp-binding`, `persistence`, tests | renderer, RHI, and editor UI packages |
| Runtime type registry | `packages/reflection` | `serialization`, sample smokes | asset pipeline and Vulkan backend |
| Asset GUIDs, source records, product keys | `packages/asset-core` | asset pipeline, tools, editors, resource runtime | renderer and RHI packages |
| Asset import decisions and product payloads | `packages/asset-pipeline` | tools and editor hosts | `asset-core`, renderer, RHI |
| Runtime resource ticket status | `packages/resource-runtime` | host applications, scene/resource systems | asset pipeline execution |
| Material resource signatures | `packages/material-core` | renderer-basic, shader-material-adapter | shader compiler and editor hosts |
| Render graph declarations and compile diagnostics | `packages/rendergraph` | renderer, RHI bridge, sample/editor smokes | Vulkan backend base target |
| Vulkan device, swapchain, GPU resources | `packages/rhi-vulkan` | renderer-basic-vulkan, host apps | RenderGraph and backend-agnostic renderer packages |
| Native viewport packets and frame-debug snapshots | `editor-native` in `apps/editor` | Studio interop adapters | managed Studio view models |
| Studio dock, command, panel, and viewport model state | `apps/studio` | Studio features and tests | C++ runtime packages |

## Dependency Rules

- Packages expose public API through `include/` or the package-specific public include directory. Other packages do not include another package's `src/`.
- `asharia::rendergraph` is backend-agnostic and must not include Vulkan headers.
- `asharia::rhi_vulkan` must not depend on RenderGraph. RenderGraph integration belongs to `asharia::rhi_vulkan_rendergraph`.
- `asharia::renderer_basic` must not record Vulkan commands. Vulkan command recording belongs to `asharia::renderer_basic_vulkan`.
- Asset import and decoder policy stays in `packages/asset-pipeline`; `asset-core` owns identity and metadata, not concrete importer behavior.
- `material-core` must not depend on Slang, Vulkan, RenderGraph, asset-pipeline, or editor hosts. `shader-material-adapter` performs shader reflection to material-signature translation.
- Runtime packages do not depend on `apps/editor`, `apps/studio`, `apps/sample-viewer`, or `tools/asset-processor`.

## Lifecycle

1. Package manifests declare package-level dependencies and target dependency intent.
2. CMake loads packages in topological order from the root `CMakeLists.txt` or through `asharia_require_package_target()` in standalone package builds.
3. Each package declares one or more `asharia::<name>` aliases.
4. Host applications aggregate package targets and own process-level lifetime.
5. RAII or explicit `create()` functions own failable runtime resources; host apps destroy them in reverse ownership order.

`future`: a generated graph should compare `asharia.package.json` target dependencies against `target_link_libraries()` and fail when they drift. Until then, review uses both files as evidence.

## Validation

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\pre-pr.ps1 -IncludeUntracked -RunCheapGates
```

Boundary checks:

```powershell
rg -n "rendergraph" packages/rhi-vulkan/CMakeLists.txt packages/rhi-vulkan/include packages/rhi-vulkan/src
rg -n "vulkan|Vk[A-Z]|vk[A-Z]" packages/rendergraph packages/renderer-basic/include/asharia/renderer_basic
rg -n "#include .*src/" engine packages apps tools -g "*.hpp" -g "*.cpp" -g "*.inl"
```

Expected results:

- The first command may find `asharia-rhi-vulkan-rendergraph`, `include-rendergraph`, and bridge files, but not a public link from `asharia-rhi-vulkan` to `asharia::rendergraph`.
- The second command should not show Vulkan API usage inside `packages/rendergraph` or the backend-agnostic `renderer_basic` public headers.
- The third command should not show cross-package `src/` includes.
