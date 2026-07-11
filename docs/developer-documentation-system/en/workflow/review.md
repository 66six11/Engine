# Workflow: Pre-Commit Review Gate

## Baseline Gates

Run before committing changes that affect code, build, workflow, or docs:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

For the complete native test gate, bootstrap Conan first and run both isolated test trees:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests --output-on-failure"
```

The ClangCL test gate promotes every clang-tidy diagnostic to an error for production and test
translation units. `.github/workflows/native-code-quality.yml` runs these CTests plus encoding,
whitespace, and asset boundary checks on Windows for pull requests, pushes to `main`, and manual
dispatches. Hosted CI does not run GPU/window smoke commands; all applicable smoke commands below
remain local pre-commit gates and must be run with both standard debug presets when the scope table
requires them.

Use `tools\pre-pr.ps1 -IncludeUntracked` to print candidate gates for the changed file set.
The `clangcl-debug` preset enables `ASHARIA_ENABLE_CLANG_TIDY=ON`; the repository does not track a separate Vulkan review script.

Useful review helper options:

| Tool | Option | Purpose |
|---|---|---|
| `tools\check-doc-sync.ps1` | `-BaseRef <ref>` | Compare against a non-`HEAD` base |
| `tools\check-doc-sync.ps1` | `-Staged` | Check staged changes only |
| `tools\check-doc-sync.ps1` | `-IncludeUntracked` | Include untracked files when comparing against `HEAD` |
| `tools\check-doc-sync.ps1` | `-NoDocsReason <text>` | Allow doc-sensitive changes without docs only when the reason is also recorded in the PR or linked issue |
| `tools\pre-pr.ps1` | `-RunCheapGates` | Run encoding, doc sync, whitespace, and asset boundary checks when selected |

## Runtime Smoke Commands

`asharia-sample-viewer` supports these smoke flags in `apps/sample-viewer/src/main.cpp`:

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-window
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-frame
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-transient
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-dynamic-rendering
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-triangle
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-depth-triangle
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mesh
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mesh-3d
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-draw-list
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mrt --frames 3
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-descriptor-layout
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-material-binding
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-fullscreen-texture
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-scene-draw-packet
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-render-view-grid-readback
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-offscreen-viewport
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-compute-dispatch
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-buffer-upload
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-texture-upload
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-renderer-format-contract
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-deferred-deletion
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-registry
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-transform
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-attributes
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-roundtrip
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-json-archive
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-migration
```

RenderGraph benchmark:

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --bench-rendergraph --warmup 10 --frames 100 --output build\rendergraph-bench.json
```

## Editor Smoke Commands

`asharia-editor` supports:

```powershell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

`asharia-editor` also supports `--project`, `--check-project`, `--check-project-json`, `--product-manifest`, `--asset-target-profile`, and `--json` for project validation. Treat `apps/editor/src/main.cpp` usage as the current source of truth.

## Asset Processor Smoke Commands

```powershell
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-dry-run
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-product-execution
```

## Scope Rules

| Changed area | Extra validation |
|---|---|
| `packages/rendergraph` | package rendergraph tests and `--smoke-rendergraph` |
| `packages/rhi-vulkan` | Vulkan/frame/resize/deferred deletion smokes |
| `packages/renderer-basic` | relevant triangle/mesh/fullscreen/compute/render-view smoke |
| `packages/asset-core` or `packages/asset-pipeline` | package tests and asset processor smokes |
| asset catalog or asset pipeline boundary code | `powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1` |
| `apps/editor` | editor smokes |
| `apps/studio` | `dotnet test apps\studio\Editor.sln` |
| documentation deployment | inspect `.github/workflows/docs-site-sync.yml`, run encoding and whitespace checks |

For frame-loop, swapchain, RenderGraph, RHI, renderer, or native viewport bridge changes, run the relevant smoke list on both `clangcl-debug` and `msvc-debug` builds. If that is blocked, record the blocker and the exact validation that must be rerun before merge.

Studio tests on Windows require the native output tree containing `editor_native.dll` and `slang.dll`. Build the matching native preset first, or pass `/p:StudioNativeBuildPreset=<preset>`.

The asset boundary gate proves that `asset-core` stays generic: concrete texture profile/importer policy patterns belong in `asset-pipeline` or editor/tool adapters, not in `asset-core`.

## Documentation Candidates

| Changed area | Candidate docs |
|---|---|
| CMake target, package manifest, package boundary | `architecture/package-dependency-map.md`, `guides/add-package-guide.md`, `workflow/build.md` |
| archive, schema, C++ binding, persistence, reflection, serialization | `architecture/data-model-and-persistence.md`, `design/reflection-serialization-design.md` |
| asset-core, asset-pipeline, project-core, resource-runtime, asset tool | `architecture/data-model-and-persistence.md`, `architecture/asset-and-material-flow.md`, `design/asset-pipeline-design.md` |
| material, shader authoring, Slang reflection, shader-material adapter | `architecture/data-model-and-persistence.md`, `architecture/asset-and-material-flow.md`, `design/material-shader-design.md` |
| RenderGraph, Vulkan RHI, renderer-basic | `architecture/package-dependency-map.md`, `architecture/rendering-and-frame-flow.md`, related design/API docs |
| native editor, editor-native, Studio viewport/frame debugger interop | `architecture/editor-runtime-boundaries.md`, `design/editor-host-design.md`, `design/studio-shell-design.md` |

## Validation

The review gate is valid when commands above either pass or the PR/issue records:

- exact command,
- exact failing step,
- blocker,
- validation that must be rerun before merge.
