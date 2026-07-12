# Workflow：提交前 Review Gate

## Baseline Gates

影响 code、build、workflow 或 docs 的提交前运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
python tools\review-vulkan-cpp.py . --exclude apps/studio --exclude apps/editor/src/native_bridge --exclude-glob "apps/editor/src/editor_shared_viewport*" --fail-on warning
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

仓库根目录 Vulkan 扫描覆盖除 `apps/studio`、`apps/editor/src/native_bridge` 和
`apps/editor/src/editor_shared_viewport*` 外的全部原生源码根；这些路径属于本审查轨道
明确排除的 Studio native bridge。排除 glob 未匹配任何文件时 scanner 会失败，使范围漂移可见。
CMake 边界审查会在每个 `target_link_libraries` 调用位置按源码顺序解释 `set` 和
`list(APPEND)` 变量，而不是使用文件末尾的最终值。

完整 native test gate 必须先 bootstrap Conan，然后运行两个独立 test tree：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests --output-on-failure"
```

ClangCL test gate 将 production/test translation units 的所有 clang-tidy diagnostics 作为 error。
`.github/workflows/native-code-quality.yml` 固定在提供 Visual Studio 17 toolchain 的 `windows-2022` runner 上运行 encoding、whitespace、asset boundary、Vulkan package boundary/safety heuristic、两编译器
build 和 CTest。ClangCL hosted build 使用 `--parallel 2`，避免 concurrent clang-tidy 超出 runner memory。Hosted CI 不运行 GPU/window smokes；下方 scope table 命中的 smoke 仍是 local
pre-commit gates，并需要使用两个 standard debug presets 运行。

`tools\pre-pr.ps1 -IncludeUntracked` 会按 changed files 打印候选 gates。
`clangcl-debug` preset 开启 `ASHARIA_ENABLE_CLANG_TIDY=ON`。`tools\review-vulkan-cpp.py`
输出供人工确认的保守提示；CI 使用 `--fail-on warning`，warning/error 会阻塞，info 仅展示。

常用 review helper 参数：

| Tool | 参数 | 用途 |
|---|---|---|
| `tools\check-doc-sync.ps1` | `-BaseRef <ref>` | 对比非 `HEAD` base |
| `tools\check-doc-sync.ps1` | `-Staged` | 只检查 staged changes |
| `tools\check-doc-sync.ps1` | `-IncludeUntracked` | 对比 `HEAD` 时包含 untracked files |
| `tools\check-doc-sync.ps1` | `-NoDocsReason <text>` | doc-sensitive files changed 但确实不需要 docs 时使用；原因也要写入 PR 或关联 Issue |
| `tools\pre-pr.ps1` | `-RunCheapGates` | 运行 encoding、doc sync、whitespace 和按需 asset boundary checks |

## Runtime Smoke Commands

`asharia-sample-viewer` 当前支持：

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

```powershell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

Editor 还支持 `--project`、`--check-project`、`--check-project-json`、`--product-manifest`、`--asset-target-profile` 和 `--json` 等项目检查相关选项；以 `apps/editor/src/main.cpp` 的 usage 为准。

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
| `packages/renderer-basic` | triangle/mesh/fullscreen/compute/render-view smokes |
| `packages/asset-core` or `packages/asset-pipeline` | package tests and asset processor smokes |
| asset catalog or asset pipeline boundary code | `powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1` |
| `apps/editor` | editor smokes |
| `apps/studio` | `dotnet test apps\studio\Editor.sln` |
| documentation deployment | inspect `.github/workflows/docs-site-sync.yml`, run encoding and whitespace checks |

修改 frame-loop、swapchain、RenderGraph、RHI、renderer 或 native viewport bridge 时，相关 smoke 清单需要在 `clangcl-debug` 和 `msvc-debug` 两套构建上运行。无法运行时记录 blocker 和 merge 前必须重跑的具体验证。

Windows 上运行 Studio tests 前，需要先有包含 `editor_native.dll` 和 `slang.dll` 的 native output tree。先构建匹配的 native preset，或传入 `/p:StudioNativeBuildPreset=<preset>`。

Asset boundary gate 证明 `asset-core` 保持 generic：具体 texture profile/importer policy patterns 应留在 `asset-pipeline` 或 editor/tool adapters，不能进入 `asset-core`。

## Documentation Candidates

| 变更区域 | 候选文档 |
|---|---|
| CMake target、package manifest、package boundary | `architecture/package-dependency-map.md`、`guides/add-package-guide.md`、`workflow/build.md` |
| archive、schema、C++ binding、persistence、reflection、serialization | `architecture/data-model-and-persistence.md`、`design/reflection-serialization-design.md` |
| asset-core、asset-pipeline、project-core、resource-runtime、asset tool | `architecture/data-model-and-persistence.md`、`architecture/asset-and-material-flow.md`、`design/asset-pipeline-design.md` |
| material、shader authoring、Slang reflection、shader-material adapter | `architecture/data-model-and-persistence.md`、`architecture/asset-and-material-flow.md`、`design/material-shader-design.md` |
| RenderGraph、Vulkan RHI、renderer-basic | `architecture/package-dependency-map.md`、`architecture/rendering-and-frame-flow.md`、相关 design/API docs |
| native editor、editor-native、Studio viewport/frame debugger interop | `architecture/editor-runtime-boundaries.md`、`design/editor-host-design.md`、`design/studio-shell-design.md` |

## Validation

Review gate 有效的条件是上述命令通过，或者 PR/Issue 记录：

- exact command,
- exact failing step,
- blocker,
- merge 前必须重跑的 validation。
