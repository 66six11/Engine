# Render Layer Architecture

本文记录当前 render 层的真实边界和源码组织，目标是让后续 RenderView、Frame Debug、RenderGraph command stream、material/asset 接入时有清晰落点。

## Package 边界

- `packages/rendergraph` 只拥有后端无关的 graph model：resource、pass、slot、params、diagnostics 和 command summary。这里不能出现 Vulkan layout、stage、access mask 或 command buffer。
- `packages/rhi-vulkan` 拥有 Vulkan context、frame loop、swapchain、VMA resource、pipeline、descriptor、deferred deletion 和 command recording 基础设施。基础 target 不依赖 RenderGraph。
- `asharia::rhi_vulkan_rendergraph` 是 RenderGraph 到 Vulkan 的适配 target，负责把抽象 state 翻译为 Vulkan barrier/layout/stage/access。
- `packages/renderer-basic` 的 `asharia::renderer_basic` 只保留后端无关的 draw item 和 builtin graph schema。
- `asharia::renderer_basic_vulkan` 负责 basic renderer 的 graph 构建、Vulkan pass callback、debug execution event 和 offscreen viewport 路径。它可以依赖 `renderer_basic`、`rendergraph`、`rhi_vulkan` 和 `rhi_vulkan_rendergraph`，但不能反向污染这些底层 package。
- `apps/editor` 只消费 renderer 输出、diagnostics 和 sampled texture；panel 不直接创建 Vulkan pipeline、graph pass 或 barrier。

2026-05-23 审查结论：

- `packages/rendergraph/include` 和 `packages/renderer-basic/include/asharia/renderer_basic` 当前未出现
  Vulkan type；`rhi_vulkan/include` 和 `rhi_vulkan/src` 当前未 include RenderGraph，RenderGraph 只出现在
  `include-rendergraph` adapter target、CMake adapter target 和 package manifest 的 target-level metadata 中。
- `render_view.hpp` 位于 `renderer_basic_vulkan` public API，当前仍带有 `VkImage` / `VkImageView` /
  `VkFormat` / `VkExtent2D`，所以它是 Vulkan RenderView contract，不是 backend-neutral `renderer_basic`
  contract。
- `recordViewFrame()` 当前会构建 RenderGraph、记录 diagnostics 和 execution events。overlay intent 会进入
  `BasicRenderViewDiagnostics`；`BasicRenderViewOverlayDesc::worldGrid` enabled 时插入
  `builtin.render-view-world-grid`，由 `renderer_basic_vulkan` 用 fullscreen triangle、push constants 中的
  inverse view-projection / optional fade / per-view LOD / grid color 参数绘制 XZ world grid；`CameraPositionNear`
  仍保留在 RenderGraph command summary 中用于 diagnostics，但不占用 world-grid shader push constant budget。RenderView policy
  只根据 camera 到 grid plane 的垂直距离计算整帧统一的 1/2/5/10 world spacing LOD，不根据水平距离或片元距离改变 LOD；低高度会锁定 base spacing，
  shader 只消费 `GridLodSettings`，`fadeStart == fadeEnd == 0` 时不做距离淡出，避免高视角像被 depth fog
  裁掉。`BasicRenderViewOverlayDesc::worldGrid` 和 `sourceOverlayIds` 都由 renderer 复制进 diagnostics；
  Frame Debug replay 从 capture 里的 world-grid desc 恢复 overlay 参数，source id 只作为溯源元数据，
  不改变 graph/pass 执行语义。只有存在 debug-world-line 数据时才插入
  `builtin.render-view-overlay` pass，把 camera / frame / debug-world-line count 作为 typed params 与 command
  summary 进入 graph，并把 world line 投影成 line-list vertex buffer 绘制到目标 attachment。后续 scene mesh、
  selection、gizmo 或更多 debug line pass 必须继续把 per-view 数据作为 renderer-owned pass input，而不是从
  diagnostics 读取。

## Public Header 布局

- `basic_renderers.hpp` 是兼容聚合头，保留旧调用方的单入口。
- `basic_renderer_descs.hpp` 放 renderer create desc。
- `basic_renderer_stats.hpp` 放 pipeline、viewport、compute 等统计结构。
- `render_view.hpp` 放 RenderView target、camera、frame params、overlay、diagnostics、execution event 和 debug preview contract。
- `descriptor_layout_smoke.hpp` 放 descriptor layout smoke 验证入口。
- `fullscreen_texture_renderer.hpp` 放 fullscreen texture、RenderView、offscreen viewport renderer。
- `basic_scene_renderers.hpp` 放 MRT、compute、triangle、mesh 3D 和 draw-list sample renderers。

## Private Source 布局

`basic_renderers.cpp` 仍是单个 translation unit。它保留共享 helper 的匿名 namespace，然后包含 `src/basic_renderers/*.inl` 私有实现分片：

- `shader_contracts.inl`
- `pipeline_layouts.inl`
- `graph_recording.inl`
- `debug_preview.inl`
- `render_view_targets.inl`
- `render_view_diagnostics.inl`
- `render_view_recording.inl`
- `descriptor_layout_smoke.inl`
- `fullscreen_texture_renderer.inl`
- `mrt_renderer.inl`
- `compute_dispatch_renderer.inl`
- `triangle_renderer.inl`
- `mesh3d_renderer.inl`
- `draw_list_renderer.inl`

这个阶段刻意不把 helper 提升成内部公共 API。`graph_recording.inl` 只覆盖 image-only graph compile、transient image preparation、execute 和 final transition 这条稳定路径；`debug_preview.inl` 只覆盖 Frame Debug replay preview 的候选图像、结果状态和 image copy pass；`render_view_targets.inl` / `render_view_diagnostics.inl` 只覆盖 RenderView target 转换、target 验证、diagnostics snapshot 和 execution event recorder；`render_view_recording.inl` 只覆盖 RenderView world-grid / debug-line overlay pass policy 与 graph pass insertion。fullscreen source/composite pass、descriptor set、pipeline readiness、debug preview 调度和 compute buffer/readback 仍留在原 owner，避免抽象过早扩大。

## 当前限制

- 实现分片不是独立 translation unit，不能提升并行编译能力。
- `renderer_basic_vulkan` 仍包含 sample renderer、editor viewport renderer 和 debug preview 支撑，后续需要继续按 RenderView pipeline、scene sample renderer、debug capture support 拆分。
- Frame Debug 的 execution event 目前是 renderer diagnostics 的轻量事件流，不等价于完整 GPU command capture。
- Debug-line overlay 的 vertex upload 仍是 renderer-owned per-frame upload buffer ring，尚未建模为 RenderGraph buffer resource；当前 RenderGraph 只观察 color target write、pass params 和 execution event。

## 执行计划

长期执行顺序记录在 `docs/planning/render-layer-refactor-plan.md`。后续不能机械照计划做，每个阶段开始前必须先检查该切片是否仍然合理、是否保持 package 边界、是否能用明确 smoke 验证。

## 下一步收敛

1. 后续再评估是否把 RenderView 路径从 `BasicFullscreenTextureRenderer` 中独立成更明确的 view renderer，fullscreen composite 只保留为消费 sampled texture 的 pass。
2. 如果新增 scene mesh、selection 或 gizmo pass，先扩展 `render_view_recording.inl` 的 pass policy / insertion helper，再决定是否需要更正式的 RenderView recorder owner。
3. 保持 `renderer_basic` 后端无关，把 Vulkan 录制和资源生命周期继续限制在 `renderer_basic_vulkan` / `rhi_vulkan`。
