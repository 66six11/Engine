# Detailed Design: Basic Renderer And Vulkan Render View

## Background

`renderer-basic` provides basic RenderGraph schemas, draw item data, and Vulkan renderer implementations. It is the main rendering layer for the sample viewer, editor viewport, and RenderGraph smokes. The package has two targets: backend-agnostic `asharia::renderer_basic` and Vulkan-specific `asharia::renderer_basic_vulkan`.

## Goals

- Register basic RenderGraph schemas and CPU-side draw data in `renderer_basic`.
- Record Vulkan commands in `renderer_basic_vulkan`.
- Support triangle, mesh, draw list, fullscreen texture, compute dispatch, render view world grid/overlay/debug preview.
- Produce `BasicRenderViewDiagnostics` for editor frame debugger/render graph panels.
- Keep Vulkan code out of the backend-agnostic target.

## Non-Goals

- Do not implement a general renderer abstraction.
- Do not put material authoring IO in renderer.
- Do not let `renderer_basic` include Vulkan headers.
- Do not own editor panel state in renderer.

## Current Constraints

- `renderer_basic` is an interface target and depends on `core`, `rendergraph`, and `shader_slang`.
- `renderer_basic_vulkan` depends on `renderer_basic`, `rhi_vulkan`, and `rhi_vulkan_rendergraph`, and privately uses `material_core`.
- Slang shaders are compiled through `asharia_add_slang_shader()`.
- RenderGraph schemas define pass type, params type, slots, and allowed commands.

## Overall Design

The backend-agnostic layer defines basic pass schemas and draw data. Callers register built-in schemas with `basicRenderGraphSchemaRegistry()` and then describe passes through RenderGraph declarations. The Vulkan layer converts compile results, resource bindings, and command summaries into Vulkan command recording.

Render View is the current shared high-level entry for editor/sample code. `BasicRenderViewDesc` contains target, camera, frame params, scene draw items, overlay desc, diagnostics, and debug preview request. The Vulkan implementation creates/reuses render targets, executes the graph, records execution events, and returns diagnostics to editor code.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `renderer_basic/render_graph_schemas.hpp` | built-in pass schema registry |
| `renderer_basic/draw_item.hpp` | draw item, mesh kind, transform data |
| `renderer_basic/clear_frame_graph.hpp` | clear graph helper |
| `renderer_basic_vulkan/render_view.hpp` | render view desc, diagnostics, debug preview |
| `renderer_basic_vulkan/frame_graph_vulkan.hpp` | Vulkan resource binding helpers |
| `renderer_basic_vulkan/basic_renderers.hpp` | aggregate Vulkan renderers |
| `renderer_basic_vulkan/fullscreen_texture_renderer.hpp` | offscreen/fullscreen texture rendering |
| `packages/renderer-basic/shaders/` | Slang shader sources |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `BasicDrawItem` | vertex/index counts, offsets, instance count | draw command shape |
| `BasicDrawListItem` | `drawItem`, `modelMatrix`, `context` | scene draw packet |
| `BasicRenderViewTarget` | image/view/format/extent/final usage | render target binding |
| `BasicRenderViewDesc` | target, view kind, camera, scene, overlay, diagnostics | render view input |
| `BasicRenderViewDiagnostics` | view info, scene diagnostics, overlay diagnostics, renderGraph, events | debug output |
| `BasicDebugPreviewRequest` | source image index, after pass, target, result | debug copy request |

## API Design

- `basicRenderGraphSchemaRegistry()` returns schemas for built-in passes.
- `BasicRenderViewDesc` is a value input struct; optional pointers enable diagnostics and debug preview.
- Vulkan renderer APIs return `Result` and use `VulkanFrameRecordContext`.
- `frame_graph_vulkan.hpp` helpers convert Vulkan targets to RenderGraph imports and bindings.

## Key Flows

### Normal Flow

1. Caller builds `BasicRenderViewDesc`.
2. Renderer imports target into RenderGraph.
3. Renderer adds scene, world grid, overlay, debug preview, and copy passes as needed.
4. Compile with `basicRenderGraphSchemaRegistry()`.
5. Map RenderGraph transitions through `rhi_vulkan_rendergraph`.
6. Record Vulkan commands.
7. Fill diagnostics and execution events.

### Failure Flow

- Unsupported target format: return error before RenderGraph import.
- Missing slot or invalid command: schema compile returns error.
- Vulkan buffer/image/pipeline failure: renderer returns error with context.
- Debug preview source unavailable: result status becomes `Unavailable` with message.

### Boundary Flow

- `BasicRenderViewTargetFinalUsage::SampledTexture` keeps offscreen target in sampled layout.
- Render view diagnostics are output data, not renderer state owners.
- Editor viewport can consume sampled texture/native packet without owning renderer internals.

## Lifetime

Renderer owns Vulkan pipeline/cache/resource helpers. Render view desc is per-frame input. Diagnostics and debug preview result are caller-owned output buffers. Offscreen render target resources use RHI resource lifetime and deferred deletion.

## Error Handling

Renderer errors use `Result` with operation context: target format, pass name, shader/pipeline, resource binding, or Vulkan call. Diagnostics should still be written when a compile result is available.

## Test Plan

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-triangle
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-depth-triangle
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mesh
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mesh-3d
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-draw-list
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-fullscreen-texture
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-render-view-grid-readback
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-renderer-format-contract
```

## Risks

- Adding Vulkan details to `renderer_basic` breaks backend boundary. Mitigation: keep Vulkan APIs under `renderer_basic_vulkan`.
- Diagnostics can drift from actual commands. Mitigation: execution events are recorded from compiled passes/commands.
- Format fallback can hide unsupported render targets. Mitigation: fail before import instead of using `RenderGraphImageFormat::Undefined`.
