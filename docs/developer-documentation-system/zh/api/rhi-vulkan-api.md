# API Reference：Vulkan RHI

## `VulkanContext`

拥有 Vulkan instance、debug messenger、surface、physical device、logical device、graphics queue 和 VMA allocator。

### `VulkanContext::create(const VulkanContextDesc& desc)`

创建 context。

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `desc.applicationName` | `std::string` | 否 | application name，默认 `Asharia Engine` |
| `desc.requiredInstanceExtensions` | `std::span<const std::string>` | 否 | window/platform required extensions |
| `desc.createSurface` | `VulkanSurfaceFactory` | 是 | 创建 `VkSurfaceKHR` 的 callback |
| `desc.enableValidation` | `bool` | 否 | 支持时启用 validation |
| `desc.debugLabels` | `VulkanDebugLabelMode` | 否 | disabled、optional 或 required |
| `desc.requireVulkan14` | `bool` | 否 | true 时要求 Vulkan 1.4 |
| `desc.externalInterop` | `VulkanExternalInteropOptions` | 否 | Win32 external memory/semaphore support flags |

返回值：

| 类型 | 说明 |
|---|---|
| `Result<VulkanContext>` | 创建成功的 context，或 `ErrorDomain::Vulkan` 错误 |

失败：

| 错误 | 触发条件 |
|---|---|
| Vulkan context error | instance/device/surface/allocator 创建失败 |
| Debug label required error | debug label mode 为 required，但 debug utils functions 不可用 |
| Interop support error | 请求的 external memory/semaphore support 不可用 |

### Accessors

| Function | 返回 |
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

拥有 swapchain frame submission resources。

### `VulkanFrameLoop::create(const VulkanContext& context, const VulkanFrameLoopDesc& desc)`

创建 swapchain frame loop state。

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `context` | `const VulkanContext&` | 是 | 已创建的 Vulkan context |
| `desc.width` | `std::uint32_t` | 否 | 初始目标宽度 |
| `desc.height` | `std::uint32_t` | 否 | 初始目标高度 |
| `desc.clearColor` | `VkClearColorValue` | 否 | 默认 clear path 使用的 clear color |

返回值：

| 类型 | 说明 |
|---|---|
| `Result<VulkanFrameLoop>` | 创建成功的 frame loop，或 Vulkan error |

### `renderFrame()`

渲染默认 clear frame。

返回值：

| 类型 | 说明 |
|---|---|
| `Result<VulkanFrameStatus>` | `Presented`、`Suboptimal`、`OutOfDate`、`Recreated` |

### `renderFrame(const VulkanFrameRecordCallback& record)`

使用调用方 callback 录制 frame commands。`VulkanFrameRecordContext` 提供 command buffer、image、image view、image index、format、extent、clear color 和 frame loop pointer。

### `setTargetExtent(width, height)`

更新期望的 swapchain extent。应在 `recreate()` 或响应 resize 的 frame rendering path 前调用。

### `recreate()`

重新创建依赖 swapchain 的 frame resources，成功时返回 `VulkanFrameStatus::Recreated`。

### Swapchain Accessors

| Function | 返回 |
|---|---|
| `format()` | 当前 swapchain `VkFormat` |
| `extent()` | 当前 swapchain `VkExtent2D` |
| `swapchainImageCount()` | swapchain image 数量 |

### `deferDeletion(callback)`

把 Vulkan cleanup work 排队到 submitted frame work 完成后执行。

### Diagnostics

| Function | 说明 |
|---|---|
| `deferredDeletionStats()` | deferred deletion queue counters |
| `debugLabelStats()` | debug label usage 和 availability |
| `timestampStats()` | timestamp query counters |
| `latestTimestampTimings()` | 最近 resolve 的 GPU regions |
| `submittedFrameEpoch()` | 最近提交的 frame epoch |
| `completedFrameEpoch()` | 最近完成的 frame epoch |

## `VulkanFrameRecordContext`

每帧 command recording context。

| Function | 说明 |
|---|---|
| `deferDeletion(callback)` | 通过当前 frame loop 排队 cleanup |
| `beginDebugLabel(name)` | 开始 debug label |
| `endDebugLabel()` | 结束 debug label |
| `setDebugObjectName(type, handle, name)` | 命名 Vulkan object |

## Resource And Pipeline Wrappers

这些 wrapper 位于基础 `asharia::rhi_vulkan` target，不依赖 RenderGraph。

| API | 职责 |
|---|---|
| `VulkanBuffer` | 基于 VMA 的 buffer allocation、mapping、upload 和 stats |
| `VulkanImage` / `VulkanImageView` | 基于 VMA 的 images 和 image views |
| `VulkanExternalImage` / `VulkanExternalSemaphore` | 在 `VulkanContextDesc::externalInterop` 请求时提供 Win32 external memory 和 semaphore interop |
| `VulkanDescriptorSetLayout`、`VulkanDescriptorPool`、`VulkanDescriptorAllocator` | descriptor layout creation、pool allocation 和 allocation stats |
| `updateVulkanDescriptorBuffers()` / `updateVulkanDescriptorImages()` | descriptor write helpers |
| `VulkanGraphicsPipeline::createDynamicRendering()` | dynamic-rendering graphics pipeline creation |

## 示例

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

## 验证方式

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-frame
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-deferred-deletion
```

检查点：

- `VkResult` 必须被检查并转换为 `Error`。
- swapchain out-of-date path 通过 `VulkanFrameStatus` 返回状态。
- deferred deletion work 在 completed frame epoch 之后 retire。
