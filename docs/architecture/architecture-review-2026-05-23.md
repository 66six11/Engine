# 当前架构与内部设计审查报告

审查日期：2026-05-23
状态更新：2026-05-26 已关闭 P2-B 的可见 debug-line renderer slice；历史 finding 保留原始审查语境，当前推进顺序以 `docs/planning/issues-and-solutions.md` 和 `docs/planning/next-development-plan.md` 为准。

本文记录本轮代码架构审查结论。它不是新设计蓝图，而是基于当前仓库事实、一手资料和本地代码路径得出的调整报告。

## 审查结论

设计审查：未通过，存在 P2/P3 级内部设计问题。

内部设计审查：未通过。当前 package / target 边界总体方向正确，但 editor、renderer、RenderGraph 与 RHI 的内部数据合同、状态模型和可观测性仍有阻塞项。继续推进 gizmo、selection outline、asset preview、material editor、multi-view diagnostics、script hot reload 或复杂 RenderGraph feature 前，必须先完成 `docs/planning/next-development-plan.md` 中 2026-05-23 新增的 A-F 门禁。

边界层面没有发现新的 P1 问题：

- `packages/rendergraph` 当前仍是后端无关接口，未公开 Vulkan type。
- `asharia::rhi_vulkan` CMake target 只链接 `asharia::core`、Vulkan、Vulkan Headers 和 VMA；RenderGraph 翻译在 `asharia::rhi_vulkan_rendergraph` target。
- `asharia::renderer_basic` 与 `asharia::renderer_basic_vulkan` target 分层清楚；Vulkan command recording 位于 Vulkan target。
- `apps/editor` 当前是 Dear ImGui host、viewport coordination 和 smoke harness；editor panel 通过 request/result 消费 viewport 服务，没有直接录制 Vulkan command。

但边界正确不等于架构健康。以下内部设计问题会影响下一阶段可扩展性和调试可信度。

## 主要 Findings

### P2. Format / capability contract 不闭合

状态：A.1 已修复。`renderer_basic_vulkan` 现在通过 `Result<RenderGraphImageFormat>` 在 renderer 入口拒绝 unsupported format，并新增 `--smoke-renderer-format-contract` 负向 smoke。后续新增 swapchain/offscreen/material format 时仍必须扩展同一 helper、Vulkan adapter 映射和 smoke。

本地证据：

- `packages/rhi-vulkan/src/vulkan_frame_loop.cpp:77` 的 `chooseSurfaceFormat()` 优先选择 `VK_FORMAT_B8G8R8A8_SRGB`，否则返回 `formats.front()`。
- `packages/rhi-vulkan/src/vulkan_frame_loop.cpp:248` 到 `:280` 将该 surface format 写入 swapchain create info，并作为 frame format 输出。
- 原始问题中，`packages/renderer-basic/include/asharia/renderer_basic_vulkan/frame_graph_vulkan.hpp:48` 到 `:54` 的 `basicRenderGraphImageFormat()` 只映射 `VK_FORMAT_B8G8R8A8_SRGB`，其他 format 变成 `RenderGraphImageFormat::Undefined`。
- 当前实现已改为 `Result<RenderGraphImageFormat>`；`packages/renderer-basic/src/basic_renderers/fullscreen_texture_renderer.inl`、`clear_frame.hpp`、`mrt_renderer.inl`、`triangle_renderer.inl`、`mesh3d_renderer.inl`、`draw_list_renderer.inl` 和 `compute_dispatch_renderer.inl` 都显式传播 unsupported format 错误。

风险：

如果平台或 surface 没有返回首选 SRGB format，swapchain 仍能创建，但 RenderGraph 侧可能拿到 `Undefined` format，错误延迟到 renderer/RHI 后续路径才暴露。Vulkan format 是资源创建、view、attachment 和 descriptor 合同的基础，不能在 swapchain 与 RenderGraph 之间隐式降级。

调整：

阶段 22 之前保持统一 format contract helper。当前只支持 `VK_FORMAT_B8G8R8A8_SRGB`；不支持时在 backbuffer / RenderView target 入口 fail early，并带 format 与调用路径。新增 format 时必须同步扩展 RenderGraph format、Vulkan adapter 映射和 negative smoke。

### P2. Editor viewport request 是单实例覆盖模型

本地证据：

- 状态：C.1 已修复。`EditorViewportCoordinator` 现在按 `panelId + EditorViewportKind` 保存 viewport slot，每个 slot
  拥有自己的 requested/presented/pending texture、latest diagnostics snapshot 和 descriptor owner key。
- `requestViewport()` 不再覆盖全局 `requestedViewport_`，同帧 Scene/Game/Preview 请求会被 `recordRequestedViews()`
  逐个消费，并把 wait stage mask 合并返回给 frame loop。
- `ImGuiTextureRegistry` 的 descriptor owner key 已和对外 `panelId` 分离，避免同一个 panel 未来同时请求不同
  `EditorViewportKind` 时发生 descriptor key 冲突。
- `--smoke-editor-viewport` 会合成同帧 Scene + Game + Preview 请求，验证 keyed diagnostics、Game debug overlay
  retention、Preview overlay stripping 和 Scene on-demand reuse。

风险：

当前 C.1 只关闭 coordinator 内部状态模型；Game View panel、asset preview UI、跨 view transient alias、
per-view profiler UI 仍未实现。可见 debug-line renderer slice 已在 2026-05-26 通过
`builtin.render-view-overlay` line-list draw 落地。

调整：

后续可以在这个 keyed slot contract 上增加真实 Game/Preview panel，但新增 UI 前仍要保持 descriptor lifetime、
diagnostics accessor 和 failure context 按 view key 分离。

### P2. RenderView camera / overlay 原始 diagnostics-only 路径

状态：B.1-B.3 renderer slice 已部分修复。`renderer_basic_vulkan` 现在会为 overlay enabled 的 RenderView 插入
`builtin.render-view-overlay` pass，把 camera position / near plane、frame params 和 debug-world-line count 作为
typed params 与 command summary 记录；存在 debug-world-line 数据时会投影为 line-list vertex buffer，并通过
renderer-owned debug-line shader/pipeline 绘制可见 world/grid line。当前剩余缺口是 camera-aware grid
range/fade policy、source overlay id diagnostics 和 pixel/readback camera-difference smoke。

原始本地证据：

- `apps/editor/src/panels/scene_view_panel.cpp:241` 提交 `EditorViewportRequest`。
- `apps/editor/src/editor_viewport_overlay_provider.cpp:50` 到 `:56` 在 Scene View grid flag 下生成 `EditorViewportOverlayPacket`。
- `apps/editor/src/editor_viewport_coordinator.cpp:294` 到 `:318` 把 camera、frame params 和 debug world lines 转成 `BasicRenderViewDesc`。
- `packages/renderer-basic/src/basic_renderers/render_view_diagnostics.inl:55` 到 `:70` 只把 camera、frame params、overlay 和 debug-world-line count 写入 diagnostics。

风险：

这证明 editor -> RenderView 的桥已经存在，但不能证明 renderer 真的使用了 camera / overlay / debug line 数据。继续做 grid/gizmo/selection outline 时，如果仍停留在 diagnostics count，会形成“UI 看起来有状态，GPU 输出不受状态影响”的半合同。

后续调整：

继续补 camera-aware grid policy、source overlay id diagnostics 和 pixel/readback camera-difference smoke。现有
smoke 已验证 Scene View graph 包含 overlay pass、Game/Preview 不接收 Scene-only pass，并记录
`DrawDebugWorldLines` execution event。

### P3. Compute dispatch 有 graph 外 GPU work

本地证据：

- `packages/renderer-basic/src/basic_renderers/compute_dispatch_renderer.inl:176` 到 `:178` 在创建 RenderGraph 前调用 `vkCmdFillBuffer()` 清 storage buffer。
- 同一文件 `:179` 到 `:186` 再把 storage buffer import 为 `RenderGraphBufferState::TransferWrite`。
- `:258` 到 `:279` 的 compute dispatch 和 readback copy 已进入 RenderGraph pass。

风险：

当前路径是合理 MVP 简化，但 graph 外 GPU work 会削弱 RenderGraph diagnostics、Frame Debug 和同步审查的可信度。后续如果继续增加 upload/copy/fill 快捷路径，RenderGraph 将无法完整解释真实 GPU command 顺序。

调整：

把 fill/clear/upload/copy 纳入 RenderGraph pass/command，或把它显式建模为 named external pre-pass，并进入 diagnostics / execution event。新增 smoke 验证外部 pre-pass 与 graph pass 顺序。

### P3. Editor context 与 app glue 有 service locator / god object 风险

本地证据：

- `apps/editor/src/editor_context.hpp:14` 到 `:47` 的 `EditorContext` 暴露 panel registry、event queue、diagnostics、frame debugger、i18n、settings、workspace 和 tools。
- `apps/editor/src/editor_panel.hpp:59` 到 `:72` 的 `EditorFrameContext` 暴露每帧 UI、diagnostics、settings、input、RenderGraph snapshot 和 viewport host。
- `apps/editor/src/editor_app.cpp` 当前约 2216 行，并包含 panel/action/tool 注册、smoke 统计、frame loop 和 ImGui/Vulkan glue；`registerEditorPanels()` 在 `:1419`，`registerEditorActions()` 在 `:1455`，`registerEditorTools()` 在 `:1635`。

风险：

当前 editor 仍处在 host MVP 阶段，这种宽 context 可以接受；但如果在此基础上直接加入 asset browser、material editor、persistent layout、script hot reload 或 inspector mutation，panel 会自然绕过 command/transaction owner，形成长期 service locator。

调整：

新增持久 editor mutation 前，先定义 command/transaction 边界和 capability-scoped context。拆出 app bootstrap/registration、smoke checks、ImGui Vulkan frame renderer，降低 `editor_app.cpp` 继续膨胀的风险。

### P3. RenderGraph public header 承载过多实现细节

本地证据：

- `packages/rendergraph/CMakeLists.txt:14` 声明 `asharia-rendergraph` 为 `INTERFACE` target。
- `packages/rendergraph/include/asharia/rendergraph/render_graph.hpp` 当前约 3993 行，承载 public builder、compile、diagnostics formatting、validation 和执行相关逻辑。

风险：

当前 header-only 有利于早期迭代和 package-local tests，但继续加入 cache、alias、multi-queue、unsafe/native pass 或更复杂 compiler diagnostics 时，public API 与实现细节会混在一起，影响编译边界和 API 稳定性。

调整：

新增复杂 graph feature 前，写 ADR 或直接拆分 public API / implementation。至少把 builder、compiled graph、diagnostics snapshot、internal compiler helpers 的稳定边界列清，并用 package test 锁住 public contract。

### P3. Frame wait stage 仍是单值模型

本地证据：

- `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_frame_loop.hpp:112` 的 `VulkanFrameRecordResult` 只有一个 `waitStageMask`。
- `packages/rhi-vulkan/src/vulkan_frame_loop.cpp:1203` 到 `:1204` 用该值作为 acquire semaphore wait stage，默认 fallback 到 `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT`。

风险：

当前 sample renderer 可以显式返回 transfer/color/compute stage，短期足够。但多 RenderView、多 attachment、多 queue 或更复杂 graph 首次使用路径出现后，单个 wait stage 可能过粗或由上层猜测。它不是当前阻塞项，但应作为 frame loop / RenderGraph integration 的后续同步审查点。

调整：

先维持单值模型；当 RenderGraph 能明确 backbuffer/imported target 的首次使用 pass 后，再从 compiled graph 推导 acquire wait stage，或在 frame callback contract 中表达 per-target first-use stage。

## 已调整的流程和计划

本轮已经把上述结论写入以下文档：

- `docs/workflow/review.md`：新增“内部代码设计审查门禁”，要求每次架构审查显式给出“内部设计审查：通过 / 未通过 / 不适用”。
- `docs/planning/next-development-plan.md`：新增 2026-05-23 计划调整，把 A-F 门禁插入阶段 21 后续和阶段 22 之前。
- `docs/planning/editor-development-plan.md`：补充 editor 内部设计阻塞规则。
- `docs/architecture/architecture-principles.md`：新增“内部代码设计也是架构”原则。
- `docs/research/sources.md`：记录本轮本地事实依据和一手资料结论。

## 外部资料依据

- Unity RenderGraph 资料强调 pass 声明资源读写、graph 编译后再执行；这支持 Asharia 要求 camera、overlay、buffer/image access 和 pass params 成为真实输入合同，而不是 diagnostics-only 字段。
- Unreal RDG 资料强调 pass parameter、resource dependency management 和 transition debugging；这支持把 descriptor/resource signature、execution event 和 graph-visible work 作为审查对象。
- O3DE Atom RPI 把 Scene、Pass、Render Pipeline、View 分开；它明确支持同一场景被不同窗口或不同模式渲染，支持 Asharia 把 Scene/Game/Preview 统一到多 RenderView 模型。
- Vulkan spec / guide 是 format、descriptor 和 synchronization 的最终依据；format、layout、stage、access、descriptor set/binding 不应靠隐式 fallback 延迟失败。

参考链接：

- Unity RenderGraph fundamentals: https://docs.unity.cn/Packages/com.unity.render-pipelines.core%4017.0/manual/render-graph-fundamentals.html
- Unity RenderGraph pipeline writing: https://docs.unity.cn/Packages/com.unity.render-pipelines.core%4016.0/manual/render-graph-writing-a-render-pipeline.html
- Unreal Render Dependency Graph: https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine
- O3DE Atom RPI System: https://www.docs.o3de.org/docs/atom-guide/dev-guide/rpi/rpi-system/
- Vulkan Specification overview: https://docs.vulkan.org/guide/latest/vulkan_spec.html
- Vulkan descriptors: https://docs.vulkan.org/spec/latest/chapters/descriptors.html
- Vulkan synchronization: https://github.khronos.org/Vulkan-Site/spec/latest/chapters/synchronization.html

## 建议推进顺序

1. 先修 P2-A format/capability contract。它是 swapchain、RenderView target、debug preview、texture upload 和 material/pipeline key 的共同基础。
2. P2-C 的 C.1 keyed multi-view request model 已关闭；P2-B 的可见 debug-line renderer slice 已关闭，后续继续推进 camera-aware grid policy 和 pixel/readback smoke。
3. 然后修 P3-D：把 compute fill、后续 upload/copy/clear 变成 graph-visible work 或 named external pre-pass。
4. 接着修 P3-E：收敛 editor command/context/app split，再进入 asset browser、material editor 或 script extension。
5. 最后在 RenderGraph 继续复杂化前处理 P3-F header/API 拆分策略。

## 验证状态

已执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
git diff --check
```

本轮是文档和流程调整，没有改 C++ 行为路径；未运行 CMake build 或 runtime smoke。
