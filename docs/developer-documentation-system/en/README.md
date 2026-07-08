# Asharia Engine Developer Documentation

This directory is the English version. It describes the current Asharia Engine codebase: a C++23 Vulkan RenderGraph engine organized around CMake package boundaries, with runtime packages, rendering backends, sample applications, editor hosts, a Studio shell, and asset processing tools.

Older material under `docs/` remains historical and migration reference. The documentation site deployment source is `docs/developer-documentation-system`, where `zh/` and `en/` are mirrored language trees.

## How To Read

| Area | Entry |
|---|---|
| Architecture | [architecture/overview.md](architecture/overview.md) |
| Package dependency map | [architecture/package-dependency-map.md](architecture/package-dependency-map.md) |
| Data model and persistence boundaries | [architecture/data-model-and-persistence.md](architecture/data-model-and-persistence.md) |
| Rendering and frame flow | [architecture/rendering-and-frame-flow.md](architecture/rendering-and-frame-flow.md) |
| Asset, material, and shader flow | [architecture/asset-and-material-flow.md](architecture/asset-and-material-flow.md) |
| Editor and runtime boundaries | [architecture/editor-runtime-boundaries.md](architecture/editor-runtime-boundaries.md) |
| Platform/window/profiling design | [design/platform-window-design.md](design/platform-window-design.md) |
| Asset catalog, import planning, and product execution design | [design/asset-pipeline-design.md](design/asset-pipeline-design.md) |
| Material, shader authoring, and Slang reflection design | [design/material-shader-design.md](design/material-shader-design.md) |
| Reflection, serialization, schema, and persistence design | [design/reflection-serialization-design.md](design/reflection-serialization-design.md) |
| Scene world and runtime resource registry design | [design/scene-resource-design.md](design/scene-resource-design.md) |
| RenderGraph design | [design/rendergraph-design.md](design/rendergraph-design.md) |
| Vulkan RHI design | [design/rhi-vulkan-design.md](design/rhi-vulkan-design.md) |
| Basic renderer and render view design | [design/renderer-basic-design.md](design/renderer-basic-design.md) |
| Native ImGui editor host design | [design/editor-host-design.md](design/editor-host-design.md) |
| Avalonia Studio shell design | [design/studio-shell-design.md](design/studio-shell-design.md) |
| Core API | [api/core-api.md](api/core-api.md) |
| RenderGraph API | [api/rendergraph-api.md](api/rendergraph-api.md) |
| Vulkan RHI API | [api/rhi-vulkan-api.md](api/rhi-vulkan-api.md) |
| Add a package | [guides/add-package-guide.md](guides/add-package-guide.md) |
| Add a RenderGraph pass | [guides/add-rendergraph-pass-guide.md](guides/add-rendergraph-pass-guide.md) |
| Build workflow | [workflow/build.md](workflow/build.md) |
| Review gate | [workflow/review.md](workflow/review.md) |
| Documentation deployment | [workflow/documentation-deployment.md](workflow/documentation-deployment.md) |
| ADRs | [adr/0001-package-first-boundaries.md](adr/0001-package-first-boundaries.md), [adr/0002-deployable-documentation-root.md](adr/0002-deployable-documentation-root.md) |
| Standards | [standards/documentation.md](standards/documentation.md), [standards/coding.md](standards/coding.md), [standards/encoding.md](standards/encoding.md) |

## Category Responsibilities

| Category | Answers | Does not own |
|---|---|---|
| `architecture/` | How the system is layered, who owns state, and who may depend on whom | Line-by-line implementation plans for a feature |
| `design/` | How a feature lands: modules, data structures, flows, error handling, and tests | Long-term roadmap and issue scheduling |
| `api/` | How interfaces are called, which parameters they take, what they return, and how they fail | Tutorial task flows |
| `guides/` | How a developer completes a concrete task | Complete API inventories |
| `workflow/` | How build, test, review, and deployment are executed | Internal design tradeoffs for a module |
| `adr/` | Why a long-lived technical decision was made and which alternatives were rejected | Temporary task notes |
| `standards/` | Naming, coding, documentation, and text encoding rules | Implementation details for the current feature |

## Current Fact Sources

- Top-level build entry points: `CMakeLists.txt`, `CMakePresets.json`, `conanfile.py`, `scripts/bootstrap-conan.ps1`.
- Package boundaries: `engine/*/CMakeLists.txt`, `packages/*/CMakeLists.txt`, `apps/*/CMakeLists.txt`, `tools/*/CMakeLists.txt`.
- Package manifests: each `asharia.package.json`.
- Public API: `engine/*/include/`, `packages/*/include/`, `packages/rhi-vulkan/include-rendergraph/`.
- Smoke and tool entry points: `apps/sample-viewer/src/main.cpp`, `apps/editor/src/main.cpp`, `tools/asset-processor/src/main.cpp`.
- Studio Avalonia shell: `apps/studio/Editor.sln`, `apps/studio/Core/`, `apps/studio/Shell/`, `apps/studio/Features/`, `apps/studio/Tests/`.

## Validation

Documentation changes should at least run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
```

If the change affects build, package boundaries, rendering, the asset pipeline, or editor behavior, also run the matching gate in [workflow/review.md](workflow/review.md).
