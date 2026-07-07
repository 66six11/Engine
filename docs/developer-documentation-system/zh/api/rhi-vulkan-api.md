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
| `desc.enableValidation` | `bool` | 否 | 启用 validation |
| `desc.debugLabels` | `VulkanDebugLabelMode` | 否 | disabled、optional 或 required |
| `desc.requireVulkan14` | `bool` | 否 | true 时要求 Vulkan 1.4 |
| `desc.externalInterop` | `VulkanExternalInteropOptions` | 否 | Win32 external memory/semaphore support flags |

返回 `Result<VulkanContext>`。失败时使用 `ErrorDomain::Vulkan`。

### Accessors

`instance()`、`instanceApiVersion()`、`surface()`、`physicalDevice()`、`device()`、`graphicsQueue()`、`graphicsQueueFamily()`、`allocator()`、`deviceInfo()`、`debugLabelFunctions()` 暴露只读 handle/info。

## `VulkanFrameLoop`

拥有 swapchain frame submission resources。

### `VulkanFrameLoop::create(const VulkanContext& context, const VulkanFrameLoopDesc& desc)`

根据 context 和初始 extent/clear color 创建 frame loop。

### `renderFrame()`

渲染默认 clear frame，返回 `Result<VulkanFrameStatus>`。

### `renderFrame(const VulkanFrameRecordCallback& record)`

使用调用方 callback 录制 frame commands。`VulkanFrameRecordContext` 提供 command buffer、image、image view、image index、format、extent、clear color 和 frame loop pointer。

### `setTargetExtent(width, height)`

更新目标 swapchain extent。

### `deferDeletion(callback)`

把 Vulkan cleanup work 排队到 submitted frame work 完成后执行。

### Diagnostics

`deferredDeletionStats()`、`debugLabelStats()`、`timestampStats()`、`latestTimestampTimings()`、`submittedFrameEpoch()`、`completedFrameEpoch()` 提供 frame loop 诊断。

## `VulkanFrameRecordContext`

| Function | 说明 |
|---|---|
| `deferDeletion(callback)` | 通过当前 frame loop 排队 cleanup |
| `beginDebugLabel(name)` | 开始 debug label |
| `endDebugLabel()` | 结束 debug label |
| `setDebugObjectName(type, handle, name)` | 命名 Vulkan object |

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
