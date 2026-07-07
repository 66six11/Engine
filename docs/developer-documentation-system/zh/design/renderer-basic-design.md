# 详细设计：Basic Renderer 与 Vulkan Render View

## 背景

`renderer-basic` 提供基础 RenderGraph schemas、draw item 数据和 Vulkan renderer implementations。它是 sample viewer、editor viewport 和 RenderGraph smokes 的主要渲染层。该 package 有两个 target：backend-agnostic `asharia::renderer_basic` 和 Vulkan-specific `asharia::renderer_basic_vulkan`。

## 目标

- 在 `renderer_basic` 中注册 basic RenderGraph schemas 和 CPU-side draw data。
- 在 `renderer_basic_vulkan` 中录制 Vulkan commands。
- 支持 triangle、mesh、draw list、fullscreen texture、compute dispatch、render view world grid/overlay/debug preview。
- 产出 `BasicRenderViewDiagnostics` 供 editor frame debugger/render graph panel 使用。
- 保持 Vulkan code 不进入 backend-agnostic target。

## 非目标

- 不实现通用 renderer abstraction。
- 不把 material authoring IO 放进 renderer。
- 不让 `renderer_basic` include Vulkan headers。
- 不在 renderer 中拥有 editor panel state。

## 当前约束

- `renderer_basic` 是 interface target，依赖 `core`、`rendergraph`、`shader_slang`。
- `renderer_basic_vulkan` 依赖 `renderer_basic`、`rhi_vulkan`、`rhi_vulkan_rendergraph`，并私有使用 `material_core`。
- Slang shaders 通过 `asharia_add_slang_shader()` 编译。
- RenderGraph schemas 定义 pass type、params type、slots 和 allowed commands。

## 总体方案

Backend-agnostic 层定义 basic pass schema 和 draw data。调用方使用 `basicRenderGraphSchemaRegistry()` 注册内置 schemas，再用 RenderGraph declaration 描述 passes。Vulkan 层把 compile result、resource bindings 和 command summaries 转换为 Vulkan command recording。

Render View 是当前 editor/sample 共享的高层入口。`BasicRenderViewDesc` 包含 target、camera、frame params、scene draw items、overlay desc、diagnostics 和 debug preview request。Vulkan implementation 创建/复用 render targets、执行 graph、记录 execution events，并把 diagnostics 交给 editor。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `renderer_basic/render_graph_schemas.hpp` | builtin pass schema registry |
| `renderer_basic/draw_item.hpp` | draw item、mesh kind、transform data |
| `renderer_basic/clear_frame_graph.hpp` | clear graph helper |
| `renderer_basic_vulkan/render_view.hpp` | render view desc、diagnostics、debug preview |
| `renderer_basic_vulkan/frame_graph_vulkan.hpp` | Vulkan resource binding helpers |
| `renderer_basic_vulkan/basic_renderers.hpp` | aggregate Vulkan renderers |
| `renderer_basic_vulkan/fullscreen_texture_renderer.hpp` | offscreen/fullscreen texture rendering |
| `packages/renderer-basic/shaders/` | Slang shader sources |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `BasicDrawItem` | vertex/index counts、offsets、instance count | draw command shape |
| `BasicDrawListItem` | `drawItem`、`modelMatrix`、`context` | scene draw packet |
| `BasicRenderViewTarget` | image/view/format/extent/final usage | render target binding |
| `BasicRenderViewDesc` | target、view kind、camera、scene、overlay、diagnostics | render view input |
| `BasicRenderViewDiagnostics` | view info、scene diagnostics、overlay diagnostics、renderGraph、events | debug output |
| `BasicDebugPreviewRequest` | source image index、after pass、target、result | debug copy request |

## API 设计

- `basicRenderGraphSchemaRegistry()` returns schemas for built-in passes.
- `BasicRenderViewDesc` is a value input struct; optional pointers enable diagnostics and debug preview.
- Vulkan renderer APIs return `Result` and use `VulkanFrameRecordContext`.
- `frame_graph_vulkan.hpp` helpers convert Vulkan targets to RenderGraph imports and bindings.

## 关键流程

### 正常流程

1. Caller builds `BasicRenderViewDesc`.
2. Renderer imports target into RenderGraph.
3. Renderer adds scene, world grid, overlay, debug preview and copy passes as needed.
4. Compile with `basicRenderGraphSchemaRegistry()`.
5. Map RenderGraph transitions through `rhi_vulkan_rendergraph`.
6. Record Vulkan commands.
7. Fill diagnostics and execution events.

### 失败流程

- unsupported target format：return error before RenderGraph import.
- missing slot or invalid command：schema compile returns error.
- Vulkan buffer/image/pipeline failure：renderer returns error with context.
- debug preview source unavailable：result status becomes `Unavailable` with message.

### 边界流程

- `BasicRenderViewTargetFinalUsage::SampledTexture` keeps offscreen target in sampled layout.
- render view diagnostics are output data, not owner of renderer state.
- editor viewport can consume sampled texture/native packet without owning renderer internals.

## 生命周期

Renderer owns Vulkan pipeline/cache/resource helpers. Render view desc is per-frame input. Diagnostics and debug preview result are caller-owned output buffers. Offscreen render target resources use RHI resource lifetime and deferred deletion.

## 错误处理

Renderer errors use `Result` with operation context: target format, pass name, shader/pipeline, resource binding, or Vulkan call. Diagnostics should still be written when a compile result is available.

## 测试方案

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

## 风险

- Adding Vulkan details to `renderer_basic` breaks backend boundary. Mitigation: keep Vulkan APIs under `renderer_basic_vulkan`.
- Diagnostics can drift from actual commands. Mitigation: execution events are recorded from compiled passes/commands.
- Format fallback can hide unsupported render targets. Mitigation: fail before import instead of using `RenderGraphImageFormat::Undefined`.
