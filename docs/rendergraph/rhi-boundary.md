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
- `RenderGraphBufferHandle`
- `RenderGraphImageFormat`
- `RenderGraphExtent2D`
- `RenderGraphImageState`
- `RenderGraphBufferState`
- `RenderGraphImageTransition`
- `RenderGraphBufferTransition`
- `RenderGraphPassContext`

资源状态应描述意图，而不是 API 细节。例如：

- `Undefined`
- `ColorAttachment`
- `ShaderRead`
- `StorageReadWrite`
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
- `vulkanBufferUsage(RenderGraphBufferState)`
- `vulkanBufferTransition(RenderGraphBufferTransition)`
- `vulkanBufferBarrier(RenderGraphBufferTransition, VkBuffer, offset, size)`

例如，RenderGraph 的 `StorageReadWrite(compute)` buffer 状态只表达“compute shader 可读写 storage
buffer”的意图；`VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`、`VK_ACCESS_2_SHADER_STORAGE_READ_BIT` 和
`VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT` 的具体映射只属于 `rhi_vulkan_rendergraph`。

这类翻译接口应由专门的适配 target 暴露，例如 `asharia::rhi_vulkan_rendergraph`。基础 Vulkan target `asharia::rhi_vulkan` 不应为了适配 RenderGraph 而公开依赖 `asharia::rendergraph`。

如果未来需要更完整的 barrier planner，也应先保留 RenderGraph 的抽象 plan，再由 Vulkan 后端生成 `VkImageMemoryBarrier2`、`VkBufferMemoryBarrier2` 和 `VkDependencyInfo`。

## 执行期资源绑定

RenderGraph 编译结果只携带 `RenderGraphImageHandle` / `RenderGraphBufferHandle` 和抽象状态，不携带 Vulkan 资源。Vulkan 后端录制命令前必须建立执行期 binding 表，把每个参与录制的 `RenderGraphImageHandle` 映射到真实的 `VkImage`，把每个参与录制的 `RenderGraphBufferHandle` 映射到真实的 `VkBuffer`。

当前 Backbuffer smoke 使用 `VulkanRenderGraphImageBinding` 显式绑定 swapchain image。后续加入 depth、transient color、postprocess 中间图或 imported texture 时，必须把这些资源加入同一 binding 表，barrier 录制不得回退到“默认当前 swapchain image”的隐式路径。

缺失 binding 是 RenderGraph/Vulkan 适配错误，应返回 `RenderGraph` 域错误并终止当前 frame 录制。

## 例外

只有明确命名为 Vulkan 后端的类型、文件或 package 可以使用 Vulkan 对象。例如：

- `asharia/rhi_vulkan/vulkan_context.hpp`
- `asharia/rhi_vulkan/vulkan_frame_loop.hpp`
- `asharia/rhi_vulkan_rendergraph/vulkan_render_graph.hpp`

通用 RenderGraph 文档或调试输出可以提到 Vulkan 概念，但不能把 Vulkan 类型作为通用 API 契约。
