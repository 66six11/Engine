# Detailed Design: Vulkan RHI Context, Frame Loop, And Resource Lifetime

## Background

`packages/rhi-vulkan` is the base Vulkan backend target. It owns Vulkan instance/device/surface/swapchain, VMA allocation, frame acquire/submit/present, deferred deletion, debug labels, timestamps, and basic buffer/image wrappers. RenderGraph-to-Vulkan mapping lives in the separate `asharia::rhi_vulkan_rendergraph` target.

## Goals

- Use `VulkanContext` to create instance, surface, device, queue, and VMA allocator.
- Use `VulkanFrameLoop` to manage swapchain, command buffer, semaphores, fence, present, and resize.
- Use `VulkanBuffer`, `VulkanImage`, `VulkanImageView`, `VulkanRenderTarget`, and `VulkanSampler` for resource lifetime.
- Use deferred deletion so resources are not destroyed while the GPU can still use them.
- Keep `asharia::rhi_vulkan` independent from RenderGraph.

## Non-Goals

- Do not declare RenderGraph passes in the base RHI target.
- Do not decide renderer strategy in the frame loop.
- Do not use `vkDeviceWaitIdle` in the render loop to hide lifetime issues.
- Do not own editor UI state in RHI.

## Current Constraints

- `rhi-vulkan` depends on `core` plus Vulkan/VMA.
- `rhi-vulkan-rendergraph` is an interface target and depends on `rhi-vulkan` and `rendergraph`.
- `VulkanContextDesc` injects surface creation through `VulkanSurfaceFactory`.
- `VulkanFrameRecordCallback` returns `VulkanFrameRecordResult`, including submit wait stage.

## Overall Design

RHI has three layers:

1. Context layer: `VulkanContext::create()` creates Vulkan instance, debug messenger, surface, physical device, logical device, graphics queue, VMA allocator, and debug label functions.
2. Frame-loop layer: `VulkanFrameLoop::renderFrame()` acquires swapchain image, records command buffer, submits, presents, and recreates on out-of-date/suboptimal state.
3. Resource layer: buffer/image/view/sampler/render-target wrappers own Vulkan handles with move-only ownership and deferred destroy support.

The RenderGraph adapter only maps abstract states to Vulkan layouts/stages/access/barriers and does not pollute base RHI.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `vulkan_context.hpp` | instance/device/surface/allocator/device info |
| `vulkan_frame_loop.hpp` | swapchain frame loop, debug labels, timestamps, deferred deletion |
| `vulkan_buffer.hpp` | VMA buffer create/upload/read |
| `vulkan_image.hpp` | image/view/render target/transient image pool/sampler |
| `deferred_deletion_queue.hpp` | epoch-based deferred cleanup |
| `vulkan_error.hpp` | `VkResult` to project error |
| `include-rendergraph/.../vulkan_render_graph.hpp` | RenderGraph to Vulkan barriers/layouts/stages mapping |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `VulkanContextDesc` | app name, required extensions, surface factory, validation, debug label mode, external interop | context creation input |
| `VulkanDeviceInfo` | device identity, queue family, timestamp support | selected device facts |
| `VulkanFrameRecordContext` | command buffer, swapchain image/view, format, extent, frameLoop | per-frame recording input |
| `VulkanFrameRecordResult` | `waitStageMask` | submit wait stage contract |
| `VulkanDeferredDeletionQueue` | callbacks by epoch | delayed destruction |
| `VulkanTransientImagePool` | key/resource pool stats | transient render target reuse |

## API Design

- `VulkanContext::create(desc)` returns a move-only context.
- `VulkanFrameLoop::create(context, desc)` owns swapchain and per-frame synchronization.
- `renderFrame(callback)` invokes the callback with `VulkanFrameRecordContext`; the callback records commands and returns wait stage.
- `deferDeletion(callback)` schedules cleanup after completed frame epoch.
- `VulkanBuffer::upload/read()` validate size and memory usage.
- `VulkanImage::deferDestroy(frame)` and `VulkanImageView::deferDestroy(frame)` bind cleanup to frame loop.

## Key Flows

### Normal Flow

1. Window creates surface factory.
2. `VulkanContext::create()` initializes Vulkan and VMA.
3. `VulkanFrameLoop::create()` creates swapchain resources.
4. `renderFrame()` acquires image.
5. Callback records renderer commands.
6. Frame loop submits with callback wait stage.
7. Present completes or reports suboptimal/out-of-date.
8. Completed frame epoch retires deferred deletions.

### Failure Flow

- Instance/device/surface creation failure: return `Result` error with context.
- Acquire/present out-of-date: return status and recreate path.
- Upload/read wrong size or memory usage: return error.
- Debug labels required but unavailable: context creation or label use returns error depending mode.

### Boundary Flow

- Resize should recreate swapchain without moving renderer strategy into frame loop.
- Retired swapchain resources remain until safe cleanup.
- External interop flags are explicit in `VulkanExternalInteropOptions`.

## Lifetime

Context owns instance/device/allocator until destruction. Frame loop owns swapchain, image views, command pool/buffer, semaphores, fence, and query pool. Resource wrappers are move-only and destroy in destructors or deferred callbacks. Deferred deletion is released after completed frame epochs.

## Error Handling

Every `VkResult` must be checked and converted to project `Error` with operation context. Shutdown cleanup may ignore already-null handles but runtime operations must not ignore failures.

## Test Plan

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-frame
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-buffer-upload
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-texture-upload
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-deferred-deletion
```

## Risks

- Incorrect wait stage can race presentation/acquire semaphores. Mitigation: callback returns explicit `waitStageMask`, and review gate checks frame callback stage.
- Destroying swapchain image views too early can race GPU use. Mitigation: retired swapchain resources and deferred deletion.
- Letting `rhi_vulkan` depend on RenderGraph breaks layering. Mitigation: keep mapping in `rhi_vulkan_rendergraph`.
