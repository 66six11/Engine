# ADR-0002：采用跨平台共享图像的嵌入式 Viewport

状态：Accepted

日期：2026-07-11

## Context

Studio 需要可 Dock、float、多实例的 Scene/Game/Preview Viewport，并支持 Windows、Linux 和 macOS。Native renderer 使用 Vulkan；Studio 使用 Avalonia。

候选路径：

- `NativeControlHost`/native child window；
- CPU readback 到 Avalonia bitmap；
- native offscreen image 通过 external GPU resource 导入 Avalonia composition；
- 所有编辑器 Viewport 都使用独立 native swapchain window。

## Decision

嵌入式 Viewport 默认采用 native offscreen render + external GPU resource + Avalonia composition。

- Windows backend 使用 capability-confirmed Win32/NT external memory/synchronization。
- Linux backend 使用 capability-confirmed opaque FD/DMA-BUF 和 semaphore path。
- macOS backend 使用 MoltenVK/Metal 的 IOSurface/Metal texture 与 `MTLSharedEvent` 路径。
- 平台资源只通过 `Asharia.Studio.EngineInterop.ViewportFrameLease` 跨边界。
- Scene、Embedded Game、Editor Window Game 和 Preview View 使用该路径。
- Standalone Game 使用 native WSI `VkSurfaceKHR`/swapchain，不经过 Avalonia。

## Alternatives

### NativeControlHost

Rejected as default。Native child window 有 airspace、z-order、overlay、Dock transform 和平台一致性限制，不适合作为多 Dock Viewport 基础。

### CPU readback

Rejected as production。可用于 fallback/debug smoke，但带宽、延迟和 CPU 开销不符合正式编辑器目标。

### Native swapchain per editor viewport

Rejected as default。Window/swapchain ownership会与 Dock/float 生命周期耦合，也难以支持 Avalonia overlay。保留给 Standalone Game。

## Consequences

Positive：

- Native renderer 保持真实渲染路径；
- Avalonia 可以正常组合工具 overlay、Dock 和多窗口；
- 逻辑 Viewport 与 Window 解耦；
- Embedded 与 Editor Window Game View 复用同一 presentation contract。

Negative：

- 三平台 handle/ownership/synchronization 不同；
- compositor 与 renderer device 必须兼容；
- embedded latency 不能代表 standalone game；
- 需要严格 frame lease、generation、backpressure 和 device lost recovery。

## Follow-ups

- 定义 EngineInterop descriptor/lease/completion contract。
- 建立 Windows、Linux、macOS capability probe 和 backend smoke。
- 实现统一 `ViewportService`、fair scheduler 和 application frame pump。
- 将当前 Windows Scene View spike 迁移出 View-owned bridge。

## Validation

- 每个平台至少两个同时渲染 Viewport。
- Dock/float/resize/minimize/device lost/import failure/shutdown matrix。
- 无 handle leak、pending frame lease、UI-thread fence wait 和无界 frame queue。
- Standalone 与 Embedded 分别记录性能基线。

## References

- Avalonia custom rendering：<https://docs.avaloniaui.net/docs/graphics-animation/custom-rendering>
- Avalonia GPU interop：<https://docs.avaloniaui.net/api/avalonia/rendering/composition/icompositiongpuinterop>
- Vulkan external synchronization：<https://docs.vulkan.org/spec/latest/chapters/synchronization.html>
- Vulkan Metal objects：<https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_metal_objects.html>
