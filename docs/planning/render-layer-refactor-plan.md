# Render Layer Refactor Plan

更新时间：2026-05-22

本文是 render 层整理的持久计划，用来避免上下文压缩或记忆上限导致方向丢失。它不是自动执行清单。每次按本文继续开发前，必须先过“合理性检查”，确认计划仍然符合当前代码事实和下一步目标。

## 当前意图

用户目标是让 render 层架构干净、可读、易调整，支撑后续 RenderView、Frame Debug、Grid、drawcall 级调试、材质/资产接入和脚本热更新边界。当前阶段先整理底层，不急着做上层 UI 或新功能。

推荐策略是分阶段收敛 `renderer_basic_vulkan` 的内部结构。先把重复样板和稳定 helper 拆清楚，再决定是否需要公共 API、独立 translation unit 或更正式的 renderer feature 框架。

## 已完成基线

- `basic_triangle_renderer.cpp` 已重命名为 `basic_renderers.cpp`。
- `basic_renderers.hpp` 已成为兼容聚合头，旧 `basic_triangle_renderer.hpp` 保留为转发兼容头。
- 公共头已按职责拆分：
  - `basic_renderer_descs.hpp`
  - `basic_renderer_stats.hpp`
  - `render_view.hpp`
  - `descriptor_layout_smoke.hpp`
  - `fullscreen_texture_renderer.hpp`
  - `basic_scene_renderers.hpp`
- `basic_renderers.cpp` 仍是单个 translation unit，但实现已拆为私有 `.inl` 分片。
- 创建期 helper 已拆出：
  - `shader_contracts.inl`
  - `pipeline_layouts.inl`
- image-only graph recording helper 已拆出：
  - `graph_recording.inl`
  - 已迁移 `BasicMrtRenderer::recordFrame`
  - 已迁移 `BasicMesh3DRenderer::recordFrame`
  - 已迁移 `BasicDrawListRenderer::recordFrame`
- debug preview helper 已拆出：
  - `debug_preview.inl`
  - 已迁移 debug image candidate、preview result 和 debug copy pass 录制支持
- RenderView helper 已拆出：
  - `render_view_targets.inl`
  - `render_view_diagnostics.inl`
  - 已迁移 target 转换、target 验证、target graph desc/binding、execution event recorder 和 diagnostics snapshot 写入
- 当前没有改变 RenderGraph 语义、Vulkan barrier/layout/stage、descriptor update、command recording 或现有 smoke 命令。

## 每步合理性检查

每次开始一个计划项前，先回答下面问题。任何一项回答不清楚，都先调整计划，不直接实现。

1. 这个切片是否仍然解决当前最大阻塞，而不是为了“看起来架构化”而拆？
2. 改动是否保持 `renderer_basic` 后端无关，只把 Vulkan 录制和资源生命周期留在 `renderer_basic_vulkan` / `rhi_vulkan`？
3. 是否避免让 `packages/rendergraph` 看到 `Vk*`、layout、stage、access、command buffer？
4. 是否避免新增公共 API？如果要新增，是否已经有稳定调用方和明确生命周期？
5. 是否保留旧 include、旧 smoke 命令和现有行为？
6. 是否能用明确验证命令证明行为没变？
7. 是否有更小的切片可以先做？
8. 是否应该因为上游需求变化而暂停、重排或删除这个计划项？

## 非目标

- 不做“大 renderer framework”。
- 不把 `.inl` 私有分片立即变成独立 translation unit。
- 不在本轮接入脚本 VM、热更新、完整 asset database、bindless 或 material editor。
- 不把 Frame Debug UI、Grid、Scene View overlay provider 混进底层整理。
- 不为了复用而把 editor-only state 放进 runtime/render package。

## Phase 1：Graph Recording Helper

状态：已完成第一版。后续只有在有第二个稳定调用组时才扩展，不为了消除单点重复继续扩大 helper。

目标：把 renderer 中重复的 graph 执行样板收敛到私有 helper。

已新增：

- `packages/renderer-basic/src/basic_renderers/graph_recording.inl`

第一版 helper 只覆盖 image-only graph 常见路径：

```cpp
[[nodiscard]] Result<void> compileExecuteAndFinalizeBasicGraph(
    const VulkanFrameRecordContext& frame,
    VkDevice device,
    VmaAllocator allocator,
    RenderGraph& graph,
    std::vector<VulkanRenderGraphImageBinding>& imageBindings,
    VulkanTransientImagePool& transientImagePool,
    std::vector<VulkanTransientImageResource>& transientImages);
```

职责只包含：

- `basicRenderGraphSchemaRegistry()`
- `graph.compile(...)`
- `prepareTransientResources(...)`
- `graph.execute(...)`
- `recordRenderGraphTransitions(frame, compiled->finalTransitions, imageBindings)`

不包含：

- `waitStageMask` 策略。仍由各 renderer 自己返回。
- diagnostics snapshot。`recordViewFrame()` 需要单独处理。
- execution event recorder。
- debug preview copy pass。
- buffer binding / compute readback。后续单独扩展。
- editor policy。

首批迁移已完成：

- `BasicMrtRenderer::recordFrame`
- `BasicMesh3DRenderer::recordFrame`
- `BasicDrawListRenderer::recordFrame`

暂不迁移：

- `BasicFullscreenTextureRenderer::recordViewFrame`，因为它绑定 diagnostics、debug preview 和 execution events。
- `BasicComputeDispatchRenderer::recordFrame`，因为它还有 buffer binding、compute dispatch 和 readback。
- `BasicTriangleRenderer::recordFrameWithDepth`，等 image-only helper 稳定后再评估。

验证：

- `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1`
- `powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1`
- `git diff --check`
- MSVC + ClangCL build
- MSVC + ClangCL：`--smoke-mrt`、`--smoke-mesh-3d`、`--smoke-draw-list`
- 补充：`--smoke-rendergraph`、`--smoke-editor-frame-debugger`

## Phase 2：Debug Preview Helper

状态：已完成第一版。它仍是 renderer-owned recording path，不是 editor panel API，也不改变 frozen diagnostics 的消费模型。

目标：把 debug preview 的 image candidate、preview result、copy pass 组织成私有模块，服务 Frame Debug replay，但不扩大为公共 API。

已新增：

- `packages/renderer-basic/src/basic_renderers/debug_preview.inl`

已迁移内容：

- `BasicRenderViewImageCandidate`
- `findBasicRenderViewImageCandidate`
- `sameRenderGraphExtent`
- `setBasicDebugPreviewResult`
- `tryAddBasicDebugPreviewPass`
- `executeBasicDebugImageCopyPass`

合理性检查重点：

- Frame Debug 是否仍只消费 frozen renderer diagnostics，不持有 Vulkan 对象。
- preview copy 是否仍然是 renderer-owned recording path，不进入 editor panel。
- 是否会影响 `recordViewFrame()` 的 diagnostics 生成顺序。

验证：

- MSVC + ClangCL：`--smoke-fullscreen-texture`、`--smoke-offscreen-viewport`
- MSVC + ClangCL：`--smoke-editor-frame-debugger`

## Phase 3：Render View Recording Split

状态：已完成第二版。已经拆出 target、diagnostics/event 支撑，并新增窄私有 `render_view_recording.inl` 来承载 RenderView overlay pass policy 和 pass insertion；`recordViewFrame()` 仍保留 fullscreen source/composite、descriptor、pipeline readiness、debug preview 调度和 graph compile/execute 编排。

目标：把 RenderView target validation、diagnostics snapshot、execution event recorder 和 offscreen/swapchain target 转换的职责从 fullscreen renderer 中分离出来。

已新增私有分片：

- `render_view_targets.inl`
- `render_view_diagnostics.inl`
- `render_view_recording.inl`

`render_view_recording.inl` 当前只做两件事：

- 从 `BasicRenderViewDesc` 和 copied debug-world-line span 推导 `BasicRenderViewPassPolicy`。
- 插入 `builtin.render-view-world-grid` 和 `builtin.render-view-overlay` passes，包括 typed params、command summary 和 Vulkan executor callback 绑定。

仍不迁移：

- `ClearFullscreenSource` 和 `FullscreenTexture` pass，因为它们属于 `BasicFullscreenTextureRenderer` 的 source/composite 路径。
- descriptor set acquisition、pipeline readiness 和 debug line vertex upload，因为它们是 renderer resource lifecycle。
- debug preview candidate/after-pass 调度，因为它依赖当前 frame replay request 和 pass index。

进入条件：

- Phase 1 的 graph helper 已稳定。
- Phase 2 的 debug preview helper 已稳定。
- `BasicFullscreenTextureRenderer::recordViewFrame()` 的 target、preview 和 diagnostics 边界已经能用私有 helper 阅读。

不急着做的事：

- 不立刻新建 `BasicRenderViewRenderer` 公共类。
- 不改 editor viewport API。
- 不改 `BasicRenderViewDiagnostics` 的字段语义。

## Phase 4：Buffer / Compute Graph Recording

目标：在 image-only helper 稳定后，再决定是否扩展到 buffer binding 和 compute readback。

候选方向：

- 新增 `compileExecuteAndFinalizeBasicGraphWithBuffers(...)`
- 或把 image/buffer binding 组合为一个 private context 结构

合理性检查重点：

- 不要为了 compute 一个调用点抽过大的 context。
- buffer final transitions 和 readback timing 不应隐藏在名字模糊的 helper 中。
- 如果 helper 让 `BasicComputeDispatchRenderer::recordFrame()` 更难读，应暂停。

验证：

- MSVC + ClangCL：`--smoke-compute-dispatch`
- MSVC + ClangCL：`--smoke-rendergraph`

## Phase 5：Renderer Feature Boundary

目标：只有在前面 helper 稳定后，再评估是否需要从 `BasicFullscreenTextureRenderer` 中拆出更明确的 view renderer。

可能方向：

- 保持现有类名，继续只做内部整理。
- 新增私有 `BasicRenderViewRecorder` helper。
- 如果调用方和生命周期足够稳定，再考虑公共 `BasicRenderViewRenderer`。

进入条件：

- editor viewport、Frame Debug、future Grid 的需求都能通过同一个 RenderView contract 表达。
- 现有 compatibility header 和 smoke 不受影响。
- 文档能清楚解释新 owner 的生命周期。

## 后续功能进入条件

Grid、gizmo、drawcall-level Frame Debug、script/hot reload、material/asset integration 都应在底层整理后再进入。进入前至少满足：

- RenderView recording 职责清楚。
- renderer execution event 和 RenderGraph command summary 的关系清楚。
- editor 只通过 provider/request/diagnostics 消费 renderer，不直接访问 Vulkan resource。
- 对应 smoke 或测试入口已定义。

## 验收总规则

每个阶段完成时必须说明：

- 改了哪些边界。
- 哪些行为明确没有改。
- 哪些命令已验证。
- 哪些计划项被推迟，以及推迟原因。

如果上下文被压缩或遗忘，继续前先读：

1. `docs/architecture/render-layer.md`
2. `docs/planning/render-layer-refactor-plan.md`
3. `packages/renderer-basic/CMakeLists.txt`
4. `packages/renderer-basic/src/basic_renderers.cpp`
