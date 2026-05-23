# 架构设计

## 目标

Asharia Engine 当前目标仍是先做一个小而完整的 Vulkan renderer，用最少功能证明 RenderGraph
从声明、编译、同步到执行、present 的完整流程。第一个稳定 frame 比大而全的抽象更重要。

架构原则是 package-first，而不是 app-first。引擎不把所有功能塞进一个 monolithic application；
它提供一个小核心和一组可组合 package，让 sample app、runtime app、editor 和后续工具按需引入能力。

跨系统开发的通用架构思想、分层规范、数据合同和功能进入前检查见
[architecture-principles.md](architecture-principles.md)。当前真实调用链和包依赖以
[flow.md](flow.md) 为准；RenderGraph/RHI 的细边界以 [../rendergraph/rhi-boundary.md](../rendergraph/rhi-boundary.md)
为准。本轮完整架构与内部设计审查报告见
[architecture-review-2026-05-23.md](architecture-review-2026-05-23.md)。

## 2026-05-23 审查基线

本次审查核对了根 `CMakeLists.txt`、各 package `CMakeLists.txt`、`asharia.package.json`、
RenderGraph/RHI/renderer/editor viewport 关键 headers 与实现，以及官方资料中的 CMake target usage、
Conan lockfile、Vulkan threading / dynamic rendering / descriptors、Unity/Unreal/Godot viewport 和
RenderGraph 资料。

当前结论：

- CMake target graph 仍是构建真相；manifest 是文档化边界和 future package registry 输入。
- `packages/rendergraph` public API 未暴露 Vulkan type；`asharia::rhi_vulkan` CMake target 只公开链接
  `asharia::core` 和 Vulkan/VMA 依赖；RenderGraph/Vulkan 翻译集中在 `asharia::rhi_vulkan_rendergraph`。
- `asharia::renderer_basic` 未暴露 Vulkan type，Vulkan 命令录制和资源绑定在
  `asharia::renderer_basic_vulkan`。
- `apps/editor` 当前是 host integration 和 smoke harness；panel 通过 backend-neutral request/result
  消费 viewport 服务，不直接创建 pipeline、descriptor 或 command buffer。
- 当前仍有临时实现形状：`renderer_basic_vulkan` 同时承载 sample renderer、RenderView/offscreen viewport、
  debug preview 和 execution event；`recordViewFrame()` 仍只把 camera 回写到 diagnostics，真正 per-view
  constants / pass input contract 尚未完成。

## 模块边界

- `engine/core`：日志、错误/result、版本和低层通用设施。不能依赖 Vulkan、GLFW、Slang、editor UI 或
  asset importer。
- `engine/platform`：平台抽象接口和最小 OS 集成，依赖 `engine/core`。
- `packages/window-glfw`：GLFW window、输入轮询和 Vulkan surface 创建，依赖 `core` / `platform`。
- `packages/profiling`：后端无关 CPU scope、frame profile、counter 和 benchmark 输出；当前不依赖
  renderer、Vulkan 或 editor。
- `packages/schema`：稳定 type/field id、value kind 和 typed metadata。
- `packages/archive`：`ArchiveValue` 和 JSON IO facade；不把第三方 JSON 类型扩散到上层 API。
- `packages/cpp-binding`：C++ object/member 与 schema field 的读写绑定。
- `packages/persistence`：组合 schema、archive 和 binding，提供 save/load/default/migration。
- `packages/scene-core`：headless World、runtime `EntityId` 和 local `Transform` baseline。
- `packages/asset-core`：asset GUID、type、handle/reference、metadata、product/cache/dependency/catalog
  的 CPU 数据模型；不拥有 GPU resource 或 editor UI。
- `packages/reflection` / `packages/serialization`：过渡兼容 package，不再承载新 editor、script、asset 或
  migration 语义。
- `packages/rendergraph`：后端无关 graph model、resource/pass/slot/schema、command summary、编译结果、
  diagnostics、abstract image/buffer state 和 transient lifetime。这里不能出现 Vulkan layout、stage/access
  mask、command buffer 或 descriptor。
- `packages/rhi-vulkan`：Vulkan context、device/queue/swapchain、VMA-backed buffer/image、pipeline、
  frame loop、deferred deletion、debug label 和 timestamp 基础设施。
- `asharia::rhi_vulkan_rendergraph`：同一 package 内的适配 target，把 RenderGraph abstract state 翻译成
  Vulkan layout/stage/access/barrier。
- `asharia::renderer_basic`：后端无关 draw item、RenderGraph builtin schema 和 renderer-facing contract。
- `asharia::renderer_basic_vulkan`：basic renderer 的 graph 构建、Vulkan pass callback、descriptor/pipeline
  绑定、offscreen viewport、RenderView diagnostics 和 debug preview。
- `packages/shader-slang`：Slang 编译、SPIR-V validation、metadata 和 reflection JSON。
- `apps/sample-viewer`：sample host 和 runtime smoke harness。它当前可直接创建 Vulkan context/frame loop，
  这是 MVP 验证事实，不是长期 runtime 边界。
- `apps/editor`：Dear ImGui editor host。它组合 `window-glfw`、`rhi-vulkan`、`renderer_basic_vulkan` 和
  ImGui backend，拥有 shell、panel/action/event、viewport coordination、texture registry、Frame Debug 和
  editor smokes；未来 `editor-core` 只能接收 backend-neutral editor state。

## 所有权模型

- `VulkanContext` 拥有 instance、debug messenger、surface、physical device 选择结果、logical device、
  graphics queue 和 VMA allocator。
- `VulkanFrameLoop` 拥有 swapchain images/image views、frame command buffer、semaphore/fence、timestamp
  query pool、debug label functions、deferred deletion retirement、acquire/submit/present/recreate 顺序。
- `VulkanBuffer`、`VulkanImage`、`VulkanImageView`、`VulkanRenderTarget` 和 `VulkanSampler` 拥有具体
  Vulkan/VMA 资源；长期资源由 renderer/RHI owner 持有，再通过 binding/import 进入 graph。
- `RenderGraph` 拥有单帧声明；`RenderGraphCompileResult` 拥有单帧编译后的 pass order、dependencies、
  abstract transitions、transient allocations、final transitions 和 diagnostics 输入。
- `renderer_basic_vulkan` 拥有 sample renderer pipeline/descriptor/buffer state，并在 frame callback 内录制
  Vulkan commands；它不拥有 swapchain present。
- `apps/editor` 拥有 editor-only UI state、ImGui context/backend lifecycle、viewport texture descriptor
  registration 和 delayed retirement。Scene View panel 只提交 `EditorViewportRequest`；
  `EditorViewportCoordinator` 才把请求转换成 sampled RenderView target 和 ImGui texture publication。

销毁顺序：

1. shutdown、swapchain recreate 或已注释 debug 路径中等待 GPU/queue idle。
2. 销毁 editor ImGui descriptors、viewport render targets 和 panel/runtime integration state。
3. 销毁 frame resources、deferred resources 和 transient targets。
4. 销毁 renderer 拥有的 buffer/image/pipeline/descriptor。
5. 销毁 swapchain。
6. 销毁 allocator-backed resources。
7. 销毁 allocator、device、surface、debug messenger、instance。

## RenderGraph 数据模型

核心概念：

- `RenderGraphImageHandle` / `RenderGraphBufferHandle`：逻辑 image/buffer 的 typed handle。
- `RenderGraphImageDesc` / `RenderGraphBufferDesc`：名称、格式/尺寸或 byte size、abstract initial/final
  state、shader stage 和 imported/transient lifetime。
- `RenderGraphPassSchema`：pass type、params type、resource slot schema、allowed command kind、culling 和
  side-effect 语义。
- `RenderGraphCommandList`：后端无关 command summary，例如 set shader/texture/constant、clear、copy、
  draw fullscreen triangle 和 dispatch。
- `RenderGraphCompileResult`：已排序 pass、resource dependencies、culled pass、transient allocations、
  abstract transitions 和 final transitions。
- `RenderGraphDiagnosticsSnapshot`：面向 Live RG View / Frame Debug 的只读观测结果，不是 renderer 下一步
  输入来源。

编译步骤：

1. 校验 pass/resource/schema/slot 声明。
2. 根据 producer/consumer 关系构建依赖并做稳定拓扑排序。
3. 计算 pass culling、side-effect 保留和非法读写诊断。
4. 计算 abstract image/buffer transition 和 final state。
5. 计算 transient resource lifetime 与 allocation plan。
6. 生成 `RenderGraphDiagnosticsSnapshot` 供 editor/debug UI 只读消费。
7. Vulkan adapter 把 abstract transitions 翻译为 `VkImageMemoryBarrier2` /
   `VkBufferMemoryBarrier2`；RenderGraph 本身不保存 Vulkan layout/stage/access。

## 当前渲染路径

当前主路径：

1. host 创建 `VulkanContext` 和 `VulkanFrameLoop`。
2. frame loop acquire swapchain image 并调用 renderer frame callback。
3. renderer/host 把 swapchain image 或 offscreen target import 成 RenderGraph resource。
4. renderer 构建 clear、triangle、MRT、compute、fullscreen texture、RenderView 或 debug preview graph。
5. `RenderGraph::compile()` 产出 pass order、abstract transitions、transient lifetime 和 diagnostics。
6. `renderer_basic_vulkan` 经 `rhi_vulkan_rendergraph` 翻译 barrier，并录制 dynamic rendering、copy、
   dispatch、descriptor/pipeline bind 和 draw commands。
7. frame loop submit、present、retire deferred deletion 和 timestamp/debug state。
8. editor path 将 sampled RenderView target 注册给 ImGui texture registry；Frame Debug / Live RG View 只读
   diagnostics snapshot。

当前建议仍保持：

- dynamic rendering 是主路径，不回退到传统 render pass/framebuffer 抽象。
- 使用单 graphics queue；async compute 和 transfer queue 在明确 queue ownership 与 smoke 前暂缓。
- 每新增一个 RenderGraph resource state，必须同步定义 abstract semantics、Vulkan translation 和 smoke。
- descriptor allocator、bindless/resource table、material/pipeline key 在 shader reflection 与 resource upload
  合同更稳定后再扩大。

## 同步策略

- CPU/GPU frame pacing 使用 per-frame fence。
- acquire/present 使用 binary semaphore，保持 swapchain 兼容性。
- GPU work submission 使用 `vkQueueSubmit2`。
- graph 内 image/buffer transition 通过 `vkCmdPipelineBarrier2` 和 synchronization2 barrier。
- frame callback 必须返回 acquire semaphore 的 wait stage；无法精确声明时只能短期使用
  `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT` 并记录待细化问题。
- 不用 `vkDeviceWaitIdle` 或 `vkQueueWaitIdle` 解决正常 render loop 同步；只允许 teardown、swapchain
  recreate、早期 MVP 简化路径或已注释 debug probe。

## Swapchain 策略

- out-of-date 或 surface 不兼容时 recreate swapchain。
- suboptimal 初期作为 warning 路径；后续再决定是否立即 recreate。
- RenderGraph resource 不直接依赖 raw swapchain image index；swapchain image 作为 imported resource。
- swapchain extent 改变时重建 framebuffer-sized transient resources 和 editor viewport targets。

## 后续扩展点

- `renderer_basic_vulkan` 按 RenderView recording、sample scene renderer、debug preview/capture support 继续拆分。
- 建立 renderer-owned per-view constants / pass input 合同，再接入 camera-aware grid、scene mesh、selection、
  gizmo 和 debug line pass。
- asset-pipeline / resource upload 把 source asset、product cache 和 runtime GPU resource 分开。
- material/pipeline key、descriptor/resource signature 和 shader reflection JSON 形成可审查合同。
- `editor-core` 只保留 selection、commands、undo/redo、workspace 和 backend-neutral viewport state。
- CPU worker、RenderThread、async compute、bindless 和 hot reload 都必须先有 ownership、fallback 和 smoke。
