# 详细设计：Vulkan RHI Context、Frame Loop 与资源生命周期

## 背景

`packages/rhi-vulkan` 是基础 Vulkan backend target。它负责 Vulkan instance/device/surface/swapchain、VMA allocation、frame acquire/submit/present、deferred deletion、debug labels、timestamps 和基础 buffer/image wrappers。RenderGraph 到 Vulkan 的映射放在独立 target `asharia::rhi_vulkan_rendergraph`。

## 目标

- 用 `VulkanContext` 集中创建 instance、surface、device、queue 和 VMA allocator。
- 用 `VulkanFrameLoop` 管理 swapchain、command buffer、semaphore、fence、present 和 resize。
- 用 `VulkanBuffer`、`VulkanImage`、`VulkanImageView`、`VulkanRenderTarget`、`VulkanSampler` 管理 Vulkan resource lifetime。
- 用 deferred deletion queue 避免 GPU 仍在使用资源时销毁。
- 保持 `asharia::rhi_vulkan` 不依赖 RenderGraph。

## 非目标

- 不在 RHI base target 中声明 RenderGraph passes。
- 不在 frame loop 中决定 renderer strategy。
- 不在 render loop 中使用 `vkDeviceWaitIdle` 掩盖生命周期问题。
- 不在 RHI 中拥有 editor UI state。

## 当前约束

- `rhi-vulkan` 依赖 `core` 和 Vulkan/VMA。
- `rhi-vulkan-rendergraph` 是 interface target，依赖 `rhi-vulkan` 和 `rendergraph`。
- `VulkanContextDesc` 通过 `VulkanSurfaceFactory` 注入 surface 创建。
- `VulkanFrameRecordCallback` 返回 `VulkanFrameRecordResult`，其中包含 submit wait stage。

## 总体方案

RHI 分成三层：

1. Context 层：`VulkanContext::create()` 创建 Vulkan instance、debug messenger、surface、physical device、logical device、graphics queue、VMA allocator 和 debug label functions。
2. Frame loop 层：`VulkanFrameLoop::renderFrame()` acquire swapchain image，record command buffer，submit，present，并在 out-of-date/suboptimal 时 recreate。
3. Resource 层：buffer/image/view/sampler/render target 封装 Vulkan handles，支持 move-only ownership 和 deferred destroy。

RenderGraph adapter 只做抽象状态到 Vulkan layout/stage/access/barrier 的映射，不反向污染 base RHI。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `vulkan_context.hpp` | instance/device/surface/allocator/device info |
| `vulkan_frame_loop.hpp` | swapchain frame loop、debug label、timestamps、deferred deletion |
| `vulkan_buffer.hpp` | VMA buffer create/upload/read |
| `vulkan_image.hpp` | image/view/render target/transient image pool/sampler |
| `deferred_deletion_queue.hpp` | epoch-based deferred cleanup |
| `vulkan_error.hpp` | `VkResult` 到 project error |
| `include-rendergraph/.../vulkan_render_graph.hpp` | RenderGraph 到 Vulkan barriers/layouts/stages mapping |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `VulkanContextDesc` | app name、required extensions、surface factory、validation、debug label mode、external interop | context creation input |
| `VulkanDeviceInfo` | device identity、queue family、timestamp support | selected device facts |
| `VulkanFrameRecordContext` | command buffer、swapchain image/view、format、extent、frameLoop | per-frame recording input |
| `VulkanFrameRecordResult` | `waitStageMask` | submit wait stage contract |
| `VulkanDeferredDeletionQueue` | callbacks by epoch | delayed destruction |
| `VulkanTransientImagePool` | key/resource pool stats | transient render target reuse |

## API 设计

- `VulkanContext::create(desc)` returns move-only context.
- `VulkanFrameLoop::create(context, desc)` owns swapchain and per-frame synchronization.
- `renderFrame(callback)` invokes callback with `VulkanFrameRecordContext`; callback records commands and returns wait stage.
- `deferDeletion(callback)` schedules cleanup after completed frame epoch.
- `VulkanBuffer::upload/read()` validate size and memory usage.
- `VulkanImage::deferDestroy(frame)` and `VulkanImageView::deferDestroy(frame)` bind cleanup to frame loop.

## 关键流程

### 正常流程

1. Window creates surface factory.
2. `VulkanContext::create()` initializes Vulkan and VMA.
3. `VulkanFrameLoop::create()` creates swapchain resources.
4. `renderFrame()` acquires image.
5. Callback records renderer commands.
6. Frame loop submits with callback wait stage.
7. Present completes or reports suboptimal/out-of-date.
8. Completed frame epoch retires deferred deletions.

### 失败流程

- instance/device/surface creation failure：return `Result` error with context.
- acquire/present out-of-date：return status and recreate path.
- upload/read wrong size or memory usage：return error.
- debug labels required but unavailable：context creation or label use returns error depending mode.

### 边界流程

- Resize should recreate swapchain without moving renderer strategy into frame loop.
- Retired swapchain resources remain until safe cleanup.
- External interop flags are explicit in `VulkanExternalInteropOptions`.

## 生命周期

Context owns instance/device/allocator until destruction. Frame loop owns swapchain, image views, command pool/buffer, semaphores, fence and query pool. Resource wrappers are move-only and destroy in destructors or deferred callbacks. Deferred deletion is released after completed frame epochs.

## 错误处理

Every `VkResult` must be checked and converted to project `Error` with operation context. Shutdown cleanup may ignore already-null handles but must not ignore failed runtime operations.

## 测试方案

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-frame
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-buffer-upload
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-texture-upload
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-deferred-deletion
```

## 风险

- Incorrect wait stage can race presentation/acquire semaphores. Mitigation: callback returns explicit `waitStageMask` and review gate checks frame callback stage.
- Destroying swapchain image views too early can race GPU use. Mitigation: retired swapchain resources and deferred deletion.
- Letting `rhi_vulkan` depend on RenderGraph breaks layering. Mitigation: keep mapping in `rhi_vulkan_rendergraph`.
