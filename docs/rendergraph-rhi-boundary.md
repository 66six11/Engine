# RenderGraph 与 RHI 边界规范

## 目标

`packages/rendergraph` 是后端无关的帧图声明与编译层。它可以描述资源、pass、读写用途、逻辑状态转换和执行回调，但不能暴露或保存具体图形 API 对象。

`packages/rhi-vulkan` 负责把 RenderGraph 的抽象状态翻译为 Vulkan 对象、layout、stage mask、access mask 和命令录制参数。

## 依赖方向

允许：

- `rendergraph -> core`
- `rhi-vulkan -> rendergraph`
- `renderer-* -> rendergraph`
- `renderer-* -> rhi-vulkan`
- `apps/* -> renderer-*` 或显式组合所需 package

禁止：

- `rendergraph -> rhi-vulkan`
- `rendergraph` public header include `<vulkan/vulkan.h>`
- `rendergraph` public API 暴露 `VkImage`、`VkFormat`、`VkImageLayout`、`VkPipelineStageFlags2`、`VkAccessFlags2`
- 在 graph pass 声明中直接写 Vulkan layout 或 barrier 参数

## RenderGraph API 应表达什么

RenderGraph 层使用引擎自己的抽象类型：

- `RenderGraphImageHandle`
- `RenderGraphImageFormat`
- `RenderGraphExtent2D`
- `RenderGraphImageState`
- `RenderGraphImageTransition`
- `RenderGraphPassContext`

资源状态应描述意图，而不是 API 细节。例如：

- `Undefined`
- `ColorAttachment`
- `TransferDst`
- `Present`

这些状态可以被 Vulkan、D3D、Metal 或测试后端分别翻译。

## Vulkan 翻译归属

Vulkan 相关翻译放在 `packages/rhi-vulkan`，例如：

- `vulkanFormat(RenderGraphImageFormat)`
- `vulkanExtent(RenderGraphExtent2D)`
- `vulkanImageLayout(RenderGraphImageState)`
- `vulkanImageUsage(RenderGraphImageState)`
- `vulkanImageTransition(RenderGraphImageTransition)`

这类翻译接口应由专门的适配 target 暴露，例如 `vke::rhi_vulkan_rendergraph`。基础 Vulkan target `vke::rhi_vulkan` 不应为了适配 RenderGraph 而公开依赖 `vke::rendergraph`。

如果未来需要更完整的 barrier planner，也应先保留 RenderGraph 的抽象 plan，再由 Vulkan 后端生成 `VkImageMemoryBarrier2` 和 `VkDependencyInfo`。

## 例外

只有明确命名为 Vulkan 后端的类型、文件或 package 可以使用 Vulkan 对象。例如：

- `vke/rhi_vulkan/vulkan_context.hpp`
- `vke/rhi_vulkan/vulkan_frame_loop.hpp`
- `vke/rhi_vulkan/vulkan_render_graph.hpp`

通用 RenderGraph 文档或调试输出可以提到 Vulkan 概念，但不能把 Vulkan 类型作为通用 API 契约。
