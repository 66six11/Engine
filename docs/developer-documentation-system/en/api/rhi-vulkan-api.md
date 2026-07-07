# API Reference: Vulkan RHI

## `VulkanContext`

Owns Vulkan instance, debug messenger, surface, physical device, logical device, graphics queue, and VMA allocator.

### `VulkanContext::create(const VulkanContextDesc& desc)`

Creates a context.

Parameters:

| Parameter | Type | Required | Description |
|---|---|---|---|
| `desc.applicationName` | `std::string` | no | Application name, default `Asharia Engine` |
| `desc.requiredInstanceExtensions` | `std::span<const std::string>` | no | Window/platform required extensions |
| `desc.createSurface` | `VulkanSurfaceFactory` | yes | Callback that creates `VkSurfaceKHR` |
| `desc.enableValidation` | `bool` | no | Enables validation when supported |
| `desc.debugLabels` | `VulkanDebugLabelMode` | no | Disabled, optional, or required debug utils |
| `desc.requireVulkan14` | `bool` | no | Requires Vulkan 1.4 when true |
| `desc.externalInterop` | `VulkanExternalInteropOptions` | no | Win32 external memory/semaphore support flags |

Return values:

| Type | Description |
|---|---|
| `Result<VulkanContext>` | Created context or `ErrorDomain::Vulkan` error |

Failures:

| Error | Trigger |
|---|---|
| Vulkan context error | instance/device/surface/allocator creation fails |
| Debug label required error | debug label mode is required and functions are unavailable |
| Interop support error | requested external memory/semaphore support is unavailable |

### Accessors

| Function | Returns |
|---|---|
| `instance()` | `VkInstance` |
| `instanceApiVersion()` | selected API version |
| `surface()` | `VkSurfaceKHR` |
| `physicalDevice()` | `VkPhysicalDevice` |
| `device()` | `VkDevice` |
| `graphicsQueue()` | `VkQueue` |
| `graphicsQueueFamily()` | queue family index |
| `allocator()` | `VmaAllocator` |
| `deviceInfo()` | `VulkanDeviceInfo` |
| `debugLabelFunctions()` | debug utils function pointers |

## `VulkanFrameLoop`

Owns swapchain frame submission resources.

### `VulkanFrameLoop::create(const VulkanContext& context, const VulkanFrameLoopDesc& desc)`

Creates swapchain frame loop state.

Parameters:

| Parameter | Type | Required | Description |
|---|---|---|---|
| `context` | `const VulkanContext&` | yes | Created Vulkan context |
| `desc.width` | `std::uint32_t` | no | Initial target width |
| `desc.height` | `std::uint32_t` | no | Initial target height |
| `desc.clearColor` | `VkClearColorValue` | no | Clear color used by default clear path |

Return values:

| Type | Description |
|---|---|
| `Result<VulkanFrameLoop>` | Created frame loop or Vulkan error |

### `renderFrame()`

Renders a default clear frame.

Return values:

| Type | Description |
|---|---|
| `Result<VulkanFrameStatus>` | `Presented`, `Suboptimal`, `OutOfDate`, `Recreated` |

### `renderFrame(const VulkanFrameRecordCallback& record)`

Renders a frame using caller-provided command recording.

`VulkanFrameRecordCallback` receives `VulkanFrameRecordContext` with command buffer, image, image view, image index, format, extent, clear color, and frame loop pointer.

### `setTargetExtent(width, height)`

Updates desired swapchain extent. Call before `recreate()` or frame rendering paths that react to resize.

### `deferDeletion(callback)`

Queues Vulkan cleanup work until submitted frame work has completed.

### Diagnostics

| Function | Description |
|---|---|
| `deferredDeletionStats()` | Deferred deletion queue counters |
| `debugLabelStats()` | Debug label usage and availability |
| `timestampStats()` | Timestamp query counters |
| `latestTimestampTimings()` | Latest resolved GPU regions |
| `submittedFrameEpoch()` | Last submitted frame epoch |
| `completedFrameEpoch()` | Last completed frame epoch |

## `VulkanFrameRecordContext`

Per-frame recording context.

| Function | Description |
|---|---|
| `deferDeletion(callback)` | Queue cleanup through current frame loop |
| `beginDebugLabel(name)` | Begin debug label if available |
| `endDebugLabel()` | End debug label |
| `setDebugObjectName(type, handle, name)` | Name Vulkan object |

## Example

```cpp
auto context = asharia::VulkanContext::create(desc);
if (!context) {
    return context.error();
}

auto frameLoop = asharia::VulkanFrameLoop::create(*context, {.width = 1280, .height = 720});
if (!frameLoop) {
    return frameLoop.error();
}

auto status = frameLoop->renderFrame([](const asharia::VulkanFrameRecordContext& frame) {
    auto label = asharia::VulkanDebugLabelScope::begin(frame, "Example");
    return asharia::VulkanFrameRecordResult{};
});
```

## Validation

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-frame
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-deferred-deletion
```

Checkpoints:

- `VkResult` is checked and converted to `Error`.
- Swapchain out-of-date paths return `VulkanFrameStatus`.
- Deferred deletion work retires after completed frame epochs.
