# Studio Composition Vulkan Viewport Spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove one docked Studio Scene View can display a native-rendered Vulkan frame through Avalonia Composition GPU interop on Windows.
**Architecture:** Keep Studio Core UI-neutral; keep Avalonia composition objects in `Features/SceneView`; keep ABI conversion in `Core/Interop/Viewports`; keep Vulkan external memory, external semaphore, rendering, and deferred GPU lifetime in `rhi-vulkan` plus `apps/editor` native runtime.
**Tech Stack:** C# 13 / .NET 10, Avalonia 12.0.4, xUnit, C++23, Vulkan 1.4, VMA, CMake presets, existing `renderer_basic_vulkan` RenderView path, existing `EditorViewportCoordinator`.

---

## Scope

Implement B0 through B3 from `docs/superpowers/specs/2026-07-06-studio-composition-vulkan-viewport-spike-design.md`.

- B0: managed Avalonia composition capability probe and Scene View status projection.
- B1: managed/native ABI for compositor-device compatibility and Vulkan handle support.
- B2: native shared Scene View image producer using Vulkan opaque NT image and semaphore handles.
- B3: Avalonia Composition consumer that imports the native present packet and updates a docked Scene View surface.

B4 resize, hidden-state recovery, and import-loss hardening remain in this plan as acceptance gates for the spike, but the first visible frame is the milestone that decides the next slice.

## Confirmed Repository Facts

- Studio resolves Avalonia `12.0.4`.
- `Compositor.TryGetCompositionGpuInterop` exists in Avalonia `12.0.4`.
- `ICompositionGpuInterop` exposes `SupportedImageHandleTypes`, `SupportedSemaphoreTypes`, `GetSynchronizationCapabilities`, `ImportImage`, `ImportSemaphore`, `IsLost`, `DeviceLuid`, and `DeviceUuid`.
- Avalonia exposes `KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle`.
- Avalonia exposes `KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle`.
- Avalonia exposes `CompositionDrawingSurface.UpdateAsync`, `UpdateWithSemaphoresAsync`, and `UpdateWithTimelineSemaphoresAsync`.
- Avalonia exposes `PlatformHandle(IntPtr, string)`.
- Current Scene View is a status-only view under `Features/SceneView`.
- Current managed native interop pattern lives under `Core/Interop/FrameDebugger/Api` and `Core/Interop/FrameDebugger/Adapters`.
- Native `editor-native` currently exports frame debugger functions only.
- Native editor viewport rendering currently records `BasicRenderViewTargetFinalUsage::SampledTexture` through `EditorViewportCoordinator`.
- `rhi_vulkan` currently has no external memory or external semaphore abstraction.

## Hard Boundaries

- No `Avalonia`, `ICompositionGpuInterop`, `CompositionDrawingSurface`, `IntPtr`, `nint`, `Vk*`, `HANDLE`, or native OS handle in `Core/Models/Viewports`.
- `Core/Interop/Viewports` may use ABI structs and `IntPtr`; that namespace is not a Core domain model.
- `Features/SceneView` may use Avalonia composition and platform handles.
- `asharia::rhi_vulkan` must not depend on RenderGraph.
- `asharia::renderer_basic` remains backend-agnostic.
- Vulkan command recording stays in native C++.
- No `vkDeviceWaitIdle` in render loops. Shutdown waits must be commented and must stay outside per-frame present.

## Files

Create:

- `apps/studio/Core/Models/Viewports/ViewportCompositionStatus.cs`
- `apps/studio/Core/Models/Viewports/ViewportCompositionCapabilitiesSnapshot.cs`
- `apps/studio/Core/Models/Viewports/ViewportNativePresentStatus.cs`
- `apps/studio/Core/Models/Viewports/ViewportNativePresentSnapshot.cs`
- `apps/studio/Core/Interop/Viewports/Api/IViewportNativeApi.cs`
- `apps/studio/Core/Interop/Viewports/Api/ViewportNativeLibraryApi.cs`
- `apps/studio/Core/Interop/Viewports/Api/ViewportNativeStatus.cs`
- `apps/studio/Core/Interop/Viewports/Api/ViewportNativeImageFormat.cs`
- `apps/studio/Core/Interop/Viewports/Api/ViewportNativeAbiHeader.cs`
- `apps/studio/Core/Interop/Viewports/Api/ViewportNativeCompatibilityRequest.cs`
- `apps/studio/Core/Interop/Viewports/Api/ViewportNativeCompatibilityResult.cs`
- `apps/studio/Core/Interop/Viewports/Api/ViewportNativePresentPacket.cs`
- `apps/studio/Core/Interop/Viewports/Adapters/ViewportNativeBridge.cs`
- `apps/studio/Features/SceneView/Interop/SceneViewCompositionCapabilityReader.cs`
- `apps/studio/Features/SceneView/Interop/SceneViewCompositionPresenter.cs`
- `apps/studio/Features/SceneView/Views/SceneViewCompositionHost.cs`
- `apps/studio/Tests/Editor.Tests/Core/Models/Viewports/ViewportCompositionModelTests.cs`
- `apps/studio/Tests/Editor.Tests/Core/Interop/Viewports/Adapters/ViewportNativeBridgeTests.cs`
- `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewCompositionCapabilityReaderTests.cs`
- `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewCompositionPresenterSourceTests.cs`
- `apps/editor/src/native_bridge/viewport_native_api.hpp`
- `apps/editor/src/native_bridge/viewport_native_api.cpp`
- `apps/editor/src/native_bridge/viewport_native_smoke.hpp`
- `apps/editor/src/native_bridge/viewport_native_smoke.cpp`
- `apps/editor/src/editor_shared_viewport_runtime.hpp`
- `apps/editor/src/editor_shared_viewport_runtime.cpp`
- `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_external_memory.hpp`
- `packages/rhi-vulkan/src/vulkan_external_memory.cpp`
- `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_external_semaphore.hpp`
- `packages/rhi-vulkan/src/vulkan_external_semaphore.cpp`

Modify:

- `apps/studio/Features/SceneView/ViewModels/SceneViewPanelViewModel.cs`
- `apps/studio/Features/SceneView/Views/SceneViewPanelView.axaml`
- `apps/studio/Features/SceneView/Views/SceneViewPanelView.axaml.cs`
- `apps/studio/Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`
- `apps/editor/CMakeLists.txt`
- `apps/editor/src/editor_smoke.cpp`
- `apps/editor/src/editor_smoke.hpp`
- `packages/rhi-vulkan/CMakeLists.txt`
- `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_context.hpp`
- `packages/rhi-vulkan/src/vulkan_context.cpp`
- `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_image.hpp`
- `packages/rhi-vulkan/src/vulkan_image.cpp`
- `docs/architecture/overview.md`
- `docs/architecture/flow.md`
- `docs/workflow/review.md`

## Task 1: B0 Core Capability Models

- [ ] **Step 1: Add model tests first**

Create `apps/studio/Tests/Editor.Tests/Core/Models/Viewports/ViewportCompositionModelTests.cs`:

```csharp
using Editor.Core.Models.Viewports;
using Xunit;

namespace Editor.Tests.Core.Models.Viewports;

public sealed class ViewportCompositionModelTests
{
    [Fact]
    public void Supported_snapshot_preserves_compositor_device_and_handle_metadata()
    {
        var capturedAt = DateTimeOffset.Parse("2026-07-06T10:00:00Z");

        var snapshot = new ViewportCompositionCapabilitiesSnapshot(
            new ViewportId("scene-view/main"),
            ViewportCompositionStatus.Supported,
            deviceLuid: "0000000000001234",
            deviceUuid: "00112233445566778899aabbccddeeff",
            imageHandleTypes: ["VulkanOpaqueNtHandle"],
            semaphoreHandleTypes: ["VulkanOpaqueNtHandle"],
            synchronizationCapabilities: ["Semaphores"],
            "Avalonia composition GPU interop is usable.",
            capturedAt);

        Assert.Equal("scene-view/main", snapshot.ViewportId.Value);
        Assert.Equal(ViewportCompositionStatus.Supported, snapshot.Status);
        Assert.Equal("0000000000001234", snapshot.DeviceLuid);
        Assert.Equal("00112233445566778899aabbccddeeff", snapshot.DeviceUuid);
        Assert.Contains("VulkanOpaqueNtHandle", snapshot.ImageHandleTypes);
        Assert.Contains("VulkanOpaqueNtHandle", snapshot.SemaphoreHandleTypes);
        Assert.Contains("Semaphores", snapshot.SynchronizationCapabilities);
        Assert.Equal(capturedAt, snapshot.CapturedAtUtc);
    }

    [Fact]
    public void Unsupported_snapshot_trims_message_and_copies_arrays()
    {
        var imageHandleTypes = new[] { "D3D11TextureNtHandle" };
        var snapshot = new ViewportCompositionCapabilitiesSnapshot(
            new ViewportId("scene-view/main"),
            ViewportCompositionStatus.VulkanOpaqueNtUnsupported,
            deviceLuid: null,
            deviceUuid: null,
            imageHandleTypes,
            semaphoreHandleTypes: [],
            synchronizationCapabilities: [],
            "  Vulkan opaque NT image handles are not supported.  ",
            DateTimeOffset.UnixEpoch);

        imageHandleTypes[0] = "changed";

        Assert.Equal("Vulkan opaque NT image handles are not supported.", snapshot.Message);
        Assert.Equal("D3D11TextureNtHandle", Assert.Single(snapshot.ImageHandleTypes));
    }
}
```

Run the focused test and confirm it fails because the models are absent:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter ViewportCompositionModelTests
```

Expected failing output contains `CS0246`.

- [ ] **Step 2: Add status enums**

Create `apps/studio/Core/Models/Viewports/ViewportCompositionStatus.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public enum ViewportCompositionStatus
{
    Unknown,
    NoTopLevel,
    NoCompositor,
    GpuInteropUnavailable,
    GpuInteropLost,
    VulkanOpaqueNtUnsupported,
    SemaphoreUnsupported,
    Supported,
}
```

Create `apps/studio/Core/Models/Viewports/ViewportNativePresentStatus.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public enum ViewportNativePresentStatus
{
    Success,
    RenderProducerUnavailable,
    UnsupportedAbi,
    UnsupportedCompositionInterop,
    DeviceMismatch,
    UnsupportedHandleType,
    RenderFailed,
    ImportFailed,
    DeviceLost,
}
```

- [ ] **Step 3: Add immutable snapshots**

Create `apps/studio/Core/Models/Viewports/ViewportCompositionCapabilitiesSnapshot.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public sealed record ViewportCompositionCapabilitiesSnapshot
{
    public ViewportCompositionCapabilitiesSnapshot(
        ViewportId viewportId,
        ViewportCompositionStatus status,
        string? deviceLuid,
        string? deviceUuid,
        IReadOnlyList<string> imageHandleTypes,
        IReadOnlyList<string> semaphoreHandleTypes,
        IReadOnlyList<string> synchronizationCapabilities,
        string message,
        DateTimeOffset capturedAtUtc)
    {
        if (viewportId.IsDefault)
        {
            throw new ArgumentException("Viewport id must be initialized.", nameof(viewportId));
        }

        if (!Enum.IsDefined(status))
        {
            throw new ArgumentOutOfRangeException(nameof(status), status, "Viewport composition status is not defined.");
        }

        ArgumentNullException.ThrowIfNull(imageHandleTypes);
        ArgumentNullException.ThrowIfNull(semaphoreHandleTypes);
        ArgumentNullException.ThrowIfNull(synchronizationCapabilities);

        ViewportId = viewportId;
        Status = status;
        DeviceLuid = string.IsNullOrWhiteSpace(deviceLuid) ? null : deviceLuid.Trim();
        DeviceUuid = string.IsNullOrWhiteSpace(deviceUuid) ? null : deviceUuid.Trim();
        ImageHandleTypes = imageHandleTypes.Select(value => value.Trim()).Where(value => value.Length > 0).ToArray();
        SemaphoreHandleTypes = semaphoreHandleTypes.Select(value => value.Trim()).Where(value => value.Length > 0).ToArray();
        SynchronizationCapabilities = synchronizationCapabilities.Select(value => value.Trim()).Where(value => value.Length > 0).ToArray();
        Message = string.IsNullOrWhiteSpace(message) ? status.ToString() : message.Trim();
        CapturedAtUtc = capturedAtUtc;
    }

    public ViewportId ViewportId { get; }
    public ViewportCompositionStatus Status { get; }
    public string? DeviceLuid { get; }
    public string? DeviceUuid { get; }
    public IReadOnlyList<string> ImageHandleTypes { get; }
    public IReadOnlyList<string> SemaphoreHandleTypes { get; }
    public IReadOnlyList<string> SynchronizationCapabilities { get; }
    public string Message { get; }
    public DateTimeOffset CapturedAtUtc { get; }
}
```

Create `apps/studio/Core/Models/Viewports/ViewportNativePresentSnapshot.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public sealed record ViewportNativePresentSnapshot
{
    public ViewportNativePresentSnapshot(
        ViewportId viewportId,
        ViewportExtent requestedExtent,
        ViewportExtent? actualExtent,
        string formatName,
        string colorSpace,
        ulong frameIndex,
        ViewportNativePresentStatus status,
        string message,
        DateTimeOffset presentedAtUtc)
    {
        if (viewportId.IsDefault)
        {
            throw new ArgumentException("Viewport id must be initialized.", nameof(viewportId));
        }

        ArgumentNullException.ThrowIfNull(requestedExtent);
        if (!Enum.IsDefined(status))
        {
            throw new ArgumentOutOfRangeException(nameof(status), status, "Viewport native present status is not defined.");
        }

        ViewportId = viewportId;
        RequestedExtent = requestedExtent;
        ActualExtent = actualExtent;
        FormatName = string.IsNullOrWhiteSpace(formatName) ? "Unknown" : formatName.Trim();
        ColorSpace = string.IsNullOrWhiteSpace(colorSpace) ? "Unknown" : colorSpace.Trim();
        FrameIndex = frameIndex;
        Status = status;
        Message = string.IsNullOrWhiteSpace(message) ? status.ToString() : message.Trim();
        PresentedAtUtc = presentedAtUtc;
    }

    public ViewportId ViewportId { get; }
    public ViewportExtent RequestedExtent { get; }
    public ViewportExtent? ActualExtent { get; }
    public string FormatName { get; }
    public string ColorSpace { get; }
    public ulong FrameIndex { get; }
    public ViewportNativePresentStatus Status { get; }
    public string Message { get; }
    public DateTimeOffset PresentedAtUtc { get; }
}
```

- [ ] **Step 4: Extend viewport model placement tests**

In `apps/studio/Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`, add these files to `Viewport_core_models_live_in_viewport_model_folder()`:

```csharp
"ViewportCompositionCapabilitiesSnapshot.cs",
"ViewportCompositionStatus.cs",
"ViewportNativePresentSnapshot.cs",
"ViewportNativePresentStatus.cs",
```

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "ViewportCompositionModelTests|Viewport_core_models_live_in_viewport_model_folder"
```

Expected passing output contains `Passed!`.

## Task 2: B0 Avalonia Capability Probe

- [ ] **Step 1: Add Scene View capability reader tests**

Create `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewCompositionCapabilityReaderTests.cs`:

```csharp
using System.IO;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewCompositionCapabilityReaderTests
{
    [Fact]
    public void Capability_reader_uses_attached_element_compositor_and_gpu_interop()
    {
        var source = LoadSource("Features", "SceneView", "Interop", "SceneViewCompositionCapabilityReader.cs");

        Assert.Contains("ElementComposition.GetElementVisual", source, StringComparison.Ordinal);
        Assert.Contains("TryGetCompositionGpuInterop", source, StringComparison.Ordinal);
        Assert.Contains("KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("GetSynchronizationCapabilities", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Editor.Core.Models.Viewports.IntPtr", source, StringComparison.Ordinal);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(new[] { root }.Concat(pathParts).ToArray()));
    }

    private static string FindRepositoryRoot()
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}
```

- [ ] **Step 2: Implement capability reader**

Create `apps/studio/Features/SceneView/Interop/SceneViewCompositionCapabilityReader.cs`:

```csharp
using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Editor.Core.Models.Viewports;

namespace Editor.Features.SceneView.Interop;

internal sealed class SceneViewCompositionCapabilityReader
{
    private const string VulkanImageHandleType =
        KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle;
    private const string VulkanSemaphoreHandleType =
        KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle;

    public async ValueTask<ViewportCompositionCapabilitiesSnapshot> ReadAsync(
        Visual host,
        ViewportId viewportId)
    {
        ArgumentNullException.ThrowIfNull(host);

        var compositor = ElementComposition.GetElementVisual(host)?.Compositor
            ?? Compositor.TryGetDefaultCompositor();
        if (TopLevel.GetTopLevel(host) is null)
        {
            return Snapshot(viewportId, ViewportCompositionStatus.NoTopLevel, null, null, [], [], [], "Scene View is not attached to a TopLevel.");
        }

        if (compositor is null)
        {
            return Snapshot(viewportId, ViewportCompositionStatus.NoCompositor, null, null, [], [], [], "Avalonia compositor is unavailable.");
        }

        var interop = await compositor.TryGetCompositionGpuInterop();
        if (interop is null)
        {
            return Snapshot(viewportId, ViewportCompositionStatus.GpuInteropUnavailable, null, null, [], [], [], "Avalonia composition GPU interop is unavailable.");
        }

        var imageTypes = interop.SupportedImageHandleTypes.ToArray();
        var semaphoreTypes = interop.SupportedSemaphoreTypes.ToArray();
        var syncCapabilities = interop.GetSynchronizationCapabilities(VulkanImageHandleType).ToString().Split(", ", StringSplitOptions.RemoveEmptyEntries);
        var status = GetStatus(interop, imageTypes, semaphoreTypes);

        return Snapshot(
            viewportId,
            status,
            interop.DeviceLuid?.ToString(),
            interop.DeviceUuid?.ToString(),
            imageTypes,
            semaphoreTypes,
            syncCapabilities,
            status == ViewportCompositionStatus.Supported
                ? "Avalonia composition GPU interop supports Vulkan opaque NT images and semaphores."
                : "Avalonia composition GPU interop is present but lacks the Vulkan opaque NT path.");
    }

    private static ViewportCompositionStatus GetStatus(
        ICompositionGpuInterop interop,
        IReadOnlyList<string> imageTypes,
        IReadOnlyList<string> semaphoreTypes)
    {
        if (interop.IsLost)
        {
            return ViewportCompositionStatus.GpuInteropLost;
        }

        if (!imageTypes.Contains(VulkanImageHandleType, StringComparer.Ordinal))
        {
            return ViewportCompositionStatus.VulkanOpaqueNtUnsupported;
        }

        return semaphoreTypes.Contains(VulkanSemaphoreHandleType, StringComparer.Ordinal)
            ? ViewportCompositionStatus.Supported
            : ViewportCompositionStatus.SemaphoreUnsupported;
    }

    private static ViewportCompositionCapabilitiesSnapshot Snapshot(
        ViewportId viewportId,
        ViewportCompositionStatus status,
        string? deviceLuid,
        string? deviceUuid,
        IReadOnlyList<string> imageTypes,
        IReadOnlyList<string> semaphoreTypes,
        IReadOnlyList<string> syncCapabilities,
        string message)
    {
        return new ViewportCompositionCapabilitiesSnapshot(
            viewportId,
            status,
            deviceLuid,
            deviceUuid,
            imageTypes,
            semaphoreTypes,
            syncCapabilities,
            message,
            DateTimeOffset.UtcNow);
    }
}
```

If `DeviceLuid` or `DeviceUuid` compiles as a non-nullable value type, adjust only the two `?.ToString()` calls to `.ToString()` and keep the snapshot as strings. Do not add these Avalonia types to Core models.

- [ ] **Step 3: Project status into Scene View**

Modify `apps/studio/Features/SceneView/ViewModels/SceneViewPanelViewModel.cs`:

```csharp
public ViewportCompositionCapabilitiesSnapshot? CompositionCapabilities { get; private set; }

public void UpdateCompositionCapabilities(ViewportCompositionCapabilitiesSnapshot snapshot)
{
    ArgumentNullException.ThrowIfNull(snapshot);
    if (snapshot.ViewportId != ViewportId)
    {
        throw new ArgumentException("Composition capability snapshot must match the Scene View viewport.", nameof(snapshot));
    }

    CompositionCapabilities = snapshot;
    OnPropertyChanged(nameof(CompositionCapabilities));
    OnPropertyChanged(nameof(ViewportStateMessage));
    OnPropertyChanged(nameof(ViewportStatusText));
}
```

Update the status text so composition capability is visible before native rendering:

```csharp
public string ViewportStatusText => CompositionCapabilities?.Status.ToString() ?? "composition pending";

public string ViewportStateMessage =>
    CompositionCapabilities is null
        ? "Scene View is waiting for Avalonia composition GPU interop probing."
        : CompositionCapabilities.Message;
```

Modify `apps/studio/Features/SceneView/Views/SceneViewPanelView.axaml.cs` so attach and size changes call the reader:

```csharp
private readonly SceneViewCompositionCapabilityReader compositionReader_ = new();

private async void ProbeCompositionCapabilities()
{
    if (DataContext is SceneViewPanelViewModel viewModel)
    {
        viewModel.UpdateCompositionCapabilities(
            await compositionReader_.ReadAsync(this, viewModel.ViewportId));
    }
}
```

Call `ProbeCompositionCapabilities()` from `AttachedToVisualTree` and after surface size changes.

- [ ] **Step 4: Run B0 managed verification**

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "CompositionCapability|ViewportComposition|SceneView"
dotnet test apps\studio\Editor.sln -c Release
```

Expected output contains `Passed!`.

Commit:

```powershell
git add apps\studio\Core\Models\Viewports apps\studio\Features\SceneView apps\studio\Tests\Editor.Tests
git commit -m "feat: probe scene view composition capabilities"
```

## Task 3: B1 Native Compatibility ABI

- [ ] **Step 1: Add managed ABI structs and bridge tests**

Add a new architecture test for Viewport interop placement to `apps/studio/Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`:

```csharp
[Fact]
public void Viewport_interop_files_separate_api_contracts_from_adapters()
{
    var root = FindRepositoryRoot();
    var apiPath = Path.Combine(root, "Core", "Interop", "Viewports", "Api", "IViewportNativeApi.cs");
    var adapterPath = Path.Combine(root, "Core", "Interop", "Viewports", "Adapters", "ViewportNativeBridge.cs");

    Assert.True(File.Exists(apiPath), "Viewport native API contract should live under Core/Interop/Viewports/Api.");
    Assert.True(File.Exists(adapterPath), "Viewport native adapter should live under Core/Interop/Viewports/Adapters.");
    Assert.Contains("namespace Editor.Core.Interop.Viewports.Api;", File.ReadAllText(apiPath), StringComparison.Ordinal);
    Assert.Contains("namespace Editor.Core.Interop.Viewports.Adapters;", File.ReadAllText(adapterPath), StringComparison.Ordinal);
}
```

Create `apps/studio/Core/Interop/Viewports/Api/ViewportNativeStatus.cs`:

```csharp
namespace Editor.Core.Interop.Viewports.Api;

internal static class ViewportNativeStatus
{
    public const uint Success = 0;
    public const uint InvalidArgument = 1;
    public const uint Unavailable = 2;
    public const uint UnsupportedAbi = 3;
    public const uint UnsupportedCompositionInterop = 4;
    public const uint DeviceMismatch = 5;
    public const uint UnsupportedHandleType = 6;
    public const uint RenderFailed = 7;
    public const uint DeviceLost = 8;
    public const uint InternalError = 9;

    public static bool IsSuccess(uint status) => status == Success;
}
```

Create `apps/studio/Core/Interop/Viewports/Api/ViewportNativeImageFormat.cs`:

```csharp
namespace Editor.Core.Interop.Viewports.Api;

internal enum ViewportNativeImageFormat : uint
{
    Unknown = 0,
    Rgba8Unorm = 1,
    Bgra8Unorm = 2,
}
```

Create `apps/studio/Core/Interop/Viewports/Api/ViewportNativeAbiHeader.cs`:

```csharp
using System.Runtime.InteropServices;

namespace Editor.Core.Interop.Viewports.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativeAbiHeader
{
    public const uint ExpectedAbiVersion = 1;

    public ViewportNativeAbiHeader(uint structSize)
    {
        AbiVersion = ExpectedAbiVersion;
        StructSize = structSize;
    }

    public uint AbiVersion { get; }
    public uint StructSize { get; }
}
```

Create `apps/studio/Core/Interop/Viewports/Api/ViewportNativeCompatibilityRequest.cs`:

```csharp
using System.Runtime.InteropServices;

namespace Editor.Core.Interop.Viewports.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativeCompatibilityRequest
{
    public static uint CurrentStructSize => checked((uint)Marshal.SizeOf<ViewportNativeCompatibilityRequest>());

    public ViewportNativeCompatibilityRequest(
        uint imageHandleType,
        uint semaphoreHandleType,
        ulong deviceLuidLowPart,
        int deviceLuidHighPart,
        uint hasDeviceLuid,
        ulong deviceUuidLow,
        ulong deviceUuidHigh,
        uint hasDeviceUuid)
    {
        Header = new ViewportNativeAbiHeader(CurrentStructSize);
        ImageHandleType = imageHandleType;
        SemaphoreHandleType = semaphoreHandleType;
        DeviceLuidLowPart = deviceLuidLowPart;
        DeviceLuidHighPart = deviceLuidHighPart;
        HasDeviceLuid = hasDeviceLuid;
        DeviceUuidLow = deviceUuidLow;
        DeviceUuidHigh = deviceUuidHigh;
        HasDeviceUuid = hasDeviceUuid;
    }

    public ViewportNativeAbiHeader Header { get; }
    public uint ImageHandleType { get; }
    public uint SemaphoreHandleType { get; }
    public ulong DeviceLuidLowPart { get; }
    public int DeviceLuidHighPart { get; }
    public uint HasDeviceLuid { get; }
    public ulong DeviceUuidLow { get; }
    public ulong DeviceUuidHigh { get; }
    public uint HasDeviceUuid { get; }
}
```

Create `apps/studio/Core/Interop/Viewports/Api/ViewportNativeCompatibilityResult.cs` with `Header`, `Status`, `ProducedImageHandleType`, `ProducedSemaphoreHandleType`, `NativeDeviceVendorId`, `NativeDeviceId`, `NativeDeviceUuidLow`, `NativeDeviceUuidHigh`, and `MessageUtf8` as a native-owned pointer plus byte length. Copy the message in `ViewportNativeBridge` and expose only managed strings in Core snapshots.

Create `apps/studio/Core/Interop/Viewports/Api/IViewportNativeApi.cs`:

```csharp
namespace Editor.Core.Interop.Viewports.Api;

internal interface IViewportNativeApi
{
    uint QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        ref ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    uint AcquirePresentPacket(
        in ViewportNativeCompatibilityRequest request,
        ref ViewportNativePresentPacket packet);

    void ReleasePresentPacket(ViewportNativePresentPacket packet);
}
```

Add `ViewportNativeLibraryApi.cs` using `LibraryImport("editor_native")` and entry points:

- `editor_viewport_query_composition_compatibility`
- `editor_viewport_release_compatibility_result`
- `editor_viewport_acquire_present_packet`
- `editor_viewport_release_present_packet`

Create `apps/studio/Tests/Editor.Tests/Core/Interop/Viewports/Adapters/ViewportNativeBridgeTests.cs` with a stub API that verifies:

- managed request sends `ExpectedAbiVersion` and `CurrentStructSize`
- `UnsupportedAbi` maps to `ViewportNativePresentStatus.UnsupportedAbi`
- `DeviceMismatch` maps to `ViewportNativePresentStatus.DeviceMismatch`
- native message buffers are released once
- bridge unavailable maps to `RenderProducerUnavailable`

- [ ] **Step 2: Add native ABI header**

Create `apps/editor/src/native_bridge/viewport_native_api.hpp`:

```cpp
#pragma once

#include "native_bridge/frame_debugger_native_api.hpp"

#include <cstdint>

extern "C" {

enum EditorViewportNativeStatus : std::uint32_t {
    EditorViewportNativeStatus_Success = 0U,
    EditorViewportNativeStatus_InvalidArgument = 1U,
    EditorViewportNativeStatus_Unavailable = 2U,
    EditorViewportNativeStatus_UnsupportedAbi = 3U,
    EditorViewportNativeStatus_UnsupportedCompositionInterop = 4U,
    EditorViewportNativeStatus_DeviceMismatch = 5U,
    EditorViewportNativeStatus_UnsupportedHandleType = 6U,
    EditorViewportNativeStatus_RenderFailed = 7U,
    EditorViewportNativeStatus_DeviceLost = 8U,
    EditorViewportNativeStatus_InternalError = 9U,
};

enum EditorViewportNativeImageHandleType : std::uint32_t {
    EditorViewportNativeImageHandleType_Unknown = 0U,
    EditorViewportNativeImageHandleType_VulkanOpaqueNt = 1U,
};

enum EditorViewportNativeSemaphoreHandleType : std::uint32_t {
    EditorViewportNativeSemaphoreHandleType_Unknown = 0U,
    EditorViewportNativeSemaphoreHandleType_VulkanOpaqueNt = 1U,
};

struct EditorViewportNativeAbiHeader {
    std::uint32_t abiVersion;
    std::uint32_t structSize;
};

struct EditorViewportNativeCompatibilityRequest {
    EditorViewportNativeAbiHeader header;
    std::uint32_t imageHandleType;
    std::uint32_t semaphoreHandleType;
    std::uint64_t deviceLuidLowPart;
    std::int32_t deviceLuidHighPart;
    std::uint32_t hasDeviceLuid;
    std::uint64_t deviceUuidLow;
    std::uint64_t deviceUuidHigh;
    std::uint32_t hasDeviceUuid;
};

struct EditorViewportNativeCompatibilityResult {
    EditorViewportNativeAbiHeader header;
    std::uint32_t status;
    std::uint32_t producedImageHandleType;
    std::uint32_t producedSemaphoreHandleType;
    std::uint32_t nativeDeviceVendorId;
    std::uint32_t nativeDeviceId;
    std::uint64_t nativeDeviceUuidLow;
    std::uint64_t nativeDeviceUuidHigh;
    void* messageUtf8;
    std::uint64_t messageByteLength;
};

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_composition_compatibility(
    const EditorViewportNativeCompatibilityRequest* request,
    EditorViewportNativeCompatibilityResult* result);

EDITOR_NATIVE_API void EDITOR_NATIVE_CALL
editor_viewport_release_compatibility_result(EditorViewportNativeCompatibilityResult result);

}
```

Create `apps/editor/src/native_bridge/viewport_native_api.cpp` with header validation, message allocation mirroring `frame_debugger_native_api.cpp`, and native status mapping. The first B1 implementation creates a headless `asharia::VulkanContext` through `EditorSharedViewportRuntime`, then compares Avalonia-provided LUID/UUID against `VulkanDeviceInfo`.

- [ ] **Step 3: Expose Vulkan device identity**

Modify `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_context.hpp`:

```cpp
struct VulkanDeviceIdentity {
    std::array<std::uint8_t, VK_LUID_SIZE> deviceLuid{};
    std::array<std::uint8_t, VK_UUID_SIZE> deviceUuid{};
    bool deviceLuidValid{};
};

struct VulkanDeviceInfo {
    std::string name;
    std::uint32_t vendorId{};
    std::uint32_t deviceId{};
    std::uint32_t apiVersion{};
    std::uint32_t graphicsQueueFamily{};
    bool graphicsQueueSupportsCompute{};
    std::uint32_t graphicsQueueTimestampValidBits{};
    float timestampPeriodNanoseconds{};
    VulkanDeviceIdentity identity;
};
```

In `vulkan_context.cpp`, query identity when selecting the physical device:

```cpp
VkPhysicalDeviceIDProperties idProperties{};
idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

VkPhysicalDeviceProperties2 properties2{};
properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
properties2.pNext = &idProperties;
vkGetPhysicalDeviceProperties2(selected.device, &properties2);
```

Copy `idProperties.deviceLUID`, `deviceUUID`, and `deviceLUIDValid` into `VulkanDeviceInfo`.

- [ ] **Step 4: Add native runtime and smoke**

Create `apps/editor/src/editor_shared_viewport_runtime.hpp`:

```cpp
#pragma once

#include <mutex>
#include <optional>

#include "asharia/rhi_vulkan/vulkan_context.hpp"

namespace asharia::editor {

    class EditorSharedViewportRuntime final {
    public:
        [[nodiscard]] static EditorSharedViewportRuntime& instance();
        [[nodiscard]] asharia::Result<const asharia::VulkanContext*> ensureContext();
        void shutdown();

    private:
        std::mutex mutex_;
        std::optional<asharia::VulkanContext> context_;
    };

} // namespace asharia::editor
```

The `.cpp` creates `VulkanContext` with:

```cpp
asharia::VulkanContextDesc{
    .applicationName = "Asharia Studio Shared Viewport",
    .requiredInstanceExtensions = {},
    .createSurface = {},
    .enableValidation = true,
    .debugLabels = asharia::VulkanDebugLabelMode::Optional,
    .requireVulkan14 = true,
}
```

Add `apps/editor/src/native_bridge/viewport_native_smoke.cpp` with:

- null request returns `InvalidArgument`
- undersized ABI header returns `UnsupportedAbi`
- unknown handle types return `UnsupportedHandleType`
- supported Vulkan opaque NT request reaches device compatibility code
- mismatched UUID returns `DeviceMismatch`

Wire the smoke through `editor_smoke.cpp` as `--smoke-editor-viewport-native`.

Modify `apps/editor/CMakeLists.txt`:

- add new source files to `editor-native`
- link `editor-native` to `asharia::rhi_vulkan`
- add native smoke source files to `asharia-editor`

- [ ] **Step 5: Run B1 verification**

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter ViewportNativeBridgeTests
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

Expected output contains `Passed!` for managed tests and a zero exit code for the native smoke.

Commit:

```powershell
git add apps\studio\Core\Interop\Viewports apps\studio\Tests\Editor.Tests\Core\Interop\Viewports apps\editor packages\rhi-vulkan
git commit -m "feat: add viewport composition native handshake"
```

## Task 4: B2 Vulkan Shared Image Producer

- [ ] **Step 1: Enable Windows external memory and semaphore extensions**

Modify `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_context.hpp`:

```cpp
struct VulkanExternalInteropOptions {
    bool enableOpaqueWin32Memory{};
    bool enableOpaqueWin32Semaphore{};
};

struct VulkanContextDesc {
    std::string applicationName{"Asharia Engine"};
    std::span<const std::string> requiredInstanceExtensions{};
    VulkanSurfaceFactory createSurface{};
    bool enableValidation{true};
    VulkanDebugLabelMode debugLabels{VulkanDebugLabelMode::Optional};
    bool requireVulkan14{true};
    VulkanExternalInteropOptions externalInterop;
};
```

In `vulkan_context.cpp`, when the options are true on Windows:

- require `VK_KHR_external_memory`
- require `VK_KHR_external_memory_win32`
- require `VK_KHR_external_semaphore`
- require `VK_KHR_external_semaphore_win32`
- load `vkGetMemoryWin32HandleKHR`
- load `vkGetSemaphoreWin32HandleKHR`

Add these function pointers to `VulkanContext` accessors:

```cpp
[[nodiscard]] PFN_vkGetMemoryWin32HandleKHR getMemoryWin32HandleFunction() const;
[[nodiscard]] PFN_vkGetSemaphoreWin32HandleKHR getSemaphoreWin32HandleFunction() const;
```

Keep non-Windows builds compiling by returning null function pointers and reporting unsupported interop from native bridge.

- [ ] **Step 2: Add external image wrapper**

Create `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_external_memory.hpp`:

```cpp
#pragma once

#include <vulkan/vulkan.h>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vma_fwd.hpp"

namespace asharia {

    struct VulkanExternalImageDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        PFN_vkGetMemoryWin32HandleKHR getMemoryWin32Handle{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
    };

    struct VulkanExportedOpaqueWin32Handle {
        void* handle{};
    };

    class VulkanExternalImage {
    public:
        VulkanExternalImage() = default;
        VulkanExternalImage(const VulkanExternalImage&) = delete;
        VulkanExternalImage& operator=(const VulkanExternalImage&) = delete;
        VulkanExternalImage(VulkanExternalImage&& other) noexcept;
        VulkanExternalImage& operator=(VulkanExternalImage&& other) noexcept;
        ~VulkanExternalImage();

        [[nodiscard]] static Result<VulkanExternalImage> create(const VulkanExternalImageDesc& desc);
        [[nodiscard]] Result<VulkanExportedOpaqueWin32Handle> exportOpaqueWin32Handle() const;
        [[nodiscard]] VkImage image() const;
        [[nodiscard]] VkImageView imageView() const;
        [[nodiscard]] VkFormat format() const;
        [[nodiscard]] VkExtent2D extent() const;
        [[nodiscard]] bool deferDestroy(const VulkanFrameRecordContext& frame);

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};
        VkImage image_{VK_NULL_HANDLE};
        VkImageView imageView_{VK_NULL_HANDLE};
        PFN_vkGetMemoryWin32HandleKHR getMemoryWin32Handle_{};
        VkFormat format_{VK_FORMAT_UNDEFINED};
        VkExtent2D extent_{};
        VkImageAspectFlags aspectMask_{VK_IMAGE_ASPECT_COLOR_BIT};
    };

} // namespace asharia
```

The `.cpp` must:

- create the image with `VkExternalMemoryImageCreateInfo{ .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT }`
- allocate through VMA with `VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT`
- use `VkExportMemoryAllocateInfo{ .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT }` through a VMA custom pool or allocator external memory path
- create the image view through existing `VulkanImageView` logic
- export with `vkGetMemoryWin32HandleKHR`
- return every `VkResult` through `vulkanError(...)`

- [ ] **Step 3: Add external semaphore wrapper**

Create `packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_external_semaphore.hpp`:

```cpp
#pragma once

#include <vulkan/vulkan.h>

#include "asharia/core/result.hpp"

namespace asharia {

    struct VulkanExternalSemaphoreDesc {
        VkDevice device{VK_NULL_HANDLE};
        PFN_vkGetSemaphoreWin32HandleKHR getSemaphoreWin32Handle{};
    };

    class VulkanExternalSemaphore {
    public:
        VulkanExternalSemaphore() = default;
        VulkanExternalSemaphore(const VulkanExternalSemaphore&) = delete;
        VulkanExternalSemaphore& operator=(const VulkanExternalSemaphore&) = delete;
        VulkanExternalSemaphore(VulkanExternalSemaphore&& other) noexcept;
        VulkanExternalSemaphore& operator=(VulkanExternalSemaphore&& other) noexcept;
        ~VulkanExternalSemaphore();

        [[nodiscard]] static Result<VulkanExternalSemaphore> create(const VulkanExternalSemaphoreDesc& desc);
        [[nodiscard]] Result<void*> exportOpaqueWin32Handle() const;
        [[nodiscard]] VkSemaphore handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkSemaphore semaphore_{VK_NULL_HANDLE};
        PFN_vkGetSemaphoreWin32HandleKHR getSemaphoreWin32Handle_{};
    };

} // namespace asharia
```

Create the semaphore with:

```cpp
VkExportSemaphoreCreateInfo exportInfo{};
exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

VkSemaphoreCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
createInfo.pNext = &exportInfo;
```

- [ ] **Step 4: Add shared viewport runtime present path**

Modify `apps/editor/src/editor_shared_viewport_runtime.hpp` to own:

- `asharia::BasicFullscreenTextureRenderer`
- `asharia::VulkanFrameLoop`
- one `VulkanExternalImage`
- one render-complete `VulkanExternalSemaphore`
- one compositor-release `VulkanExternalSemaphore`

Add:

```cpp
struct EditorSharedViewportPresentDesc {
    std::string_view panelId;
    EditorViewportKind kind{EditorViewportKind::Scene};
    EditorExtent2D extent;
};

struct EditorSharedViewportPresentPacket {
    void* imageHandle{};
    void* waitSemaphoreHandle{};
    void* signalSemaphoreHandle{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkExtent2D extent{};
    std::uint64_t frameIndex{};
};

[[nodiscard]] asharia::Result<EditorSharedViewportPresentPacket>
renderSceneViewFrame(EditorSharedViewportPresentDesc desc);
```

The implementation records a single RenderView frame into `VulkanExternalImage` with final usage `BasicRenderViewTargetFinalUsage::SampledTexture`, signals `waitSemaphoreHandle` after render completion, and returns native-owned duplicated NT handles through the ABI packet. The native release function closes handles and retires images only after compositor update completion is reported or the present packet is superseded.

Update `editor_viewport_acquire_present_packet` to call `renderSceneViewFrame(...)` and write:

- `status`
- `imageHandle`
- `waitSemaphoreHandle`
- `signalSemaphoreHandle`
- `width`
- `height`
- `format`
- `frameIndex`
- message

- [ ] **Step 5: Add B2 smoke**

Extend `--smoke-editor-viewport-native`:

- create the headless external-interoperable Vulkan context
- query compatibility
- render a 320 x 180 Scene View frame
- verify image and semaphore handles are non-null
- release the packet
- render a second frame at 640 x 360
- verify the old packet was released and the new extent is returned

Run:

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
```

Expected output is a zero exit code for all three smoke commands.

Commit:

```powershell
git add packages\rhi-vulkan apps\editor
git commit -m "feat: produce shared viewport vulkan images"
```

## Task 5: B3 Avalonia Composition Consumer

- [ ] **Step 1: Add source-level presenter tests**

Create `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewCompositionPresenterSourceTests.cs`:

```csharp
using System.IO;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewCompositionPresenterSourceTests
{
    [Fact]
    public void Presenter_imports_vulkan_nt_image_and_uses_semaphore_update()
    {
        var source = LoadSource("Features", "SceneView", "Interop", "SceneViewCompositionPresenter.cs");

        Assert.Contains("KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle", source, StringComparison.Ordinal);
        Assert.Contains("new PlatformHandle(", source, StringComparison.Ordinal);
        Assert.Contains("ImportImage(", source, StringComparison.Ordinal);
        Assert.Contains("ImportSemaphore(", source, StringComparison.Ordinal);
        Assert.Contains("UpdateWithSemaphoresAsync(", source, StringComparison.Ordinal);
        Assert.Contains("ReleasePresentPacket", source, StringComparison.Ordinal);
        Assert.DoesNotContain("WaitForSingleObject", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Thread.Sleep", source, StringComparison.Ordinal);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(new[] { root }.Concat(pathParts).ToArray()));
    }

    private static string FindRepositoryRoot()
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}
```

- [ ] **Step 2: Add a composition host control**

Create `apps/studio/Features/SceneView/Views/SceneViewCompositionHost.cs`:

```csharp
using Avalonia;
using Avalonia.Controls;
using Avalonia.LogicalTree;
using Avalonia.Rendering.Composition;
using Avalonia.VisualTree;

namespace Editor.Features.SceneView.Views;

internal sealed class SceneViewCompositionHost : Control
{
    private CompositionSurfaceVisual? visual_;
    private CompositionDrawingSurface? surface_;

    public CompositionDrawingSurface? Surface => surface_;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        AttachCompositionVisual();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        ElementComposition.SetElementChildVisual(this, null);
        visual_?.Dispose();
        visual_ = null;
        Surface?.Dispose();
        surface_ = null;
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        if (change.Property == BoundsProperty && visual_ is not null)
        {
            visual_.Size = Bounds.Size;
        }

        base.OnPropertyChanged(change);
    }

    private void AttachCompositionVisual()
    {
        var selfVisual = ElementComposition.GetElementVisual(this);
        if (selfVisual is null)
        {
            return;
        }

        var compositor = selfVisual.Compositor;
        surface_ = compositor.CreateDrawingSurface();
        visual_ = compositor.CreateSurfaceVisual();
        visual_.Size = Bounds.Size;
        visual_.Surface = surface_;
        ElementComposition.SetElementChildVisual(this, visual_);
    }
}
```

The host follows the Avalonia GPU interop sample shape: `ElementComposition.GetElementVisual(this)`, `compositor.CreateDrawingSurface()`, `compositor.CreateSurfaceVisual()`, set `visual.Surface`, then set the child visual.

- [ ] **Step 3: Implement presenter import and release**

Create `apps/studio/Features/SceneView/Interop/SceneViewCompositionPresenter.cs`:

```csharp
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Editor.Core.Interop.Viewports.Adapters;
using Editor.Core.Interop.Viewports.Api;
using Editor.Core.Models.Viewports;

namespace Editor.Features.SceneView.Interop;

internal sealed class SceneViewCompositionPresenter
{
    private const string ImageHandleType =
        KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle;
    private const string SemaphoreHandleType =
        KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle;

    private readonly ViewportNativeBridge bridge_;

    public SceneViewCompositionPresenter(ViewportNativeBridge bridge)
    {
        bridge_ = bridge;
    }

    public async Task<ViewportNativePresentSnapshot> PresentAsync(
        ICompositionGpuInterop interop,
        CompositionDrawingSurface surface,
        ViewportNativePresentPacket packet)
    {
        ArgumentNullException.ThrowIfNull(interop);
        ArgumentNullException.ThrowIfNull(surface);

        try
        {
            var imageHandle = new PlatformHandle(packet.ImageHandle, ImageHandleType);
            var waitHandle = new PlatformHandle(packet.WaitSemaphoreHandle, SemaphoreHandleType);
            var signalHandle = new PlatformHandle(packet.SignalSemaphoreHandle, SemaphoreHandleType);

            var imageProperties = packet.CreateAvaloniaImageProperties();
            using var image = interop.ImportImage(imageHandle, imageProperties);
            using var waitSemaphore = interop.ImportSemaphore(waitHandle);
            using var signalSemaphore = interop.ImportSemaphore(signalHandle);

            await surface.UpdateWithSemaphoresAsync(image, waitSemaphore, signalSemaphore);

            return packet.ToSnapshot(ViewportNativePresentStatus.Success, "Presented native Vulkan viewport frame.");
        }
        catch (Exception ex) when (ex is InvalidOperationException or NotSupportedException)
        {
            return packet.ToSnapshot(ViewportNativePresentStatus.ImportFailed, ex.Message);
        }
        finally
        {
            bridge_.ReleasePresentPacket(packet);
        }
    }
}
```

`ViewportNativePresentPacket.CreateAvaloniaImageProperties()` must be implemented in the interop API namespace:

```csharp
internal PlatformGraphicsExternalImageProperties CreateAvaloniaImageProperties()
{
    return new PlatformGraphicsExternalImageProperties
    {
        Width = checked((int)WidthPixels),
        Height = checked((int)HeightPixels),
        Format = Format == ViewportNativeImageFormat.Rgba8Unorm
            ? PlatformGraphicsExternalImageFormat.R8G8B8A8UNorm
            : PlatformGraphicsExternalImageFormat.B8G8R8A8UNorm,
        MemoryOffset = 0UL,
        MemorySize = MemorySizeBytes,
        TopLeftOrigin = true,
    };
}
```

Native must populate `MemorySizeBytes` from the external image allocation size and must restrict B3 formats to `VK_FORMAT_R8G8B8A8_UNORM` or `VK_FORMAT_B8G8R8A8_UNORM`.

The `finally` release is required. Managed code imports handles and releases the native packet, but native remains the Vulkan owner.

- [ ] **Step 4: Wire Scene View surface**

Modify `apps/studio/Features/SceneView/Views/SceneViewPanelView.axaml` so the Scene View content row contains:

```xml
<views:SceneViewCompositionHost x:Name="CompositionHost"
                                Focusable="True" />
```

Modify `SceneViewPanelView.axaml.cs`:

- probe composition capabilities on attach
- call native compatibility only when B0 status is `Supported`
- request a native present packet only for visible, non-zero extents
- call `SceneViewCompositionPresenter.PresentAsync(...)`
- drop a frame if a previous present task is still running
- update `SceneViewPanelViewModel` with `ViewportNativePresentSnapshot`

Add a private guard:

```csharp
private Task? pendingPresent_;

private bool CanStartPresent() => pendingPresent_ is null || pendingPresent_.IsCompleted;
```

Do not block the UI thread. All present paths return to status UI on failure.

- [ ] **Step 5: Run B3 verification**

```powershell
dotnet build apps\studio\Editor.csproj -c Release
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"
dotnet test apps\studio\Editor.sln -c Release
```

Expected output contains `Build succeeded.` and `Passed!`.

Manual visual check:

- launch Studio
- open Scene View
- confirm status changes from composition probing to compatibility or presented frame
- resize Scene View
- hide and show the docked panel
- confirm there is no UI freeze while import or native render fails

Commit:

```powershell
git add apps\studio\Features\SceneView apps\studio\Core\Interop\Viewports apps\studio\Tests\Editor.Tests
git commit -m "feat: present native viewport through avalonia composition"
```

## Task 6: Documentation And Full Gate

- [ ] **Step 1: Update architecture docs**

Update `docs/architecture/overview.md`:

- Studio Core viewport models own scheduler/capability/native-present snapshots only.
- Studio Scene View owns Avalonia composition probing, imported image wrappers, drawing surface, and fallback status UI.
- Native bridge owns ABI packets and native packet release.
- `rhi_vulkan` owns external image and semaphore wrappers.
- `renderer_basic_vulkan` remains the RenderView recorder.

Update `docs/architecture/flow.md` with a sequence from Scene View attach to Avalonia import/update.

Update `docs/workflow/review.md` to include:

```powershell
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

- [ ] **Step 2: Run repository checks**

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected output has no encoding violations and no whitespace errors.

- [ ] **Step 3: Run pre-commit native builds**

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Expected output ends with successful builds for both presets.

- [ ] **Step 4: Run viewport smokes on both builds**

```powershell
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
```

Expected output is zero exit code for every smoke.

Commit docs:

```powershell
git add docs\architecture\overview.md docs\architecture\flow.md docs\workflow\review.md
git commit -m "docs: document studio composition viewport flow"
```

## Final Review Checklist

- [ ] `Core/Models/Viewports` contains no Avalonia, Vulkan, OS handle, pointer, native bridge, RenderGraph, or renderer references.
- [ ] `Core/Interop/Viewports` is the only managed Core area with native ABI structs.
- [ ] `Features/SceneView` is the only managed area with Avalonia composition import/update logic.
- [ ] `editor-native` exports compatibility and present packet functions.
- [ ] Native packet release is always called from managed `finally`.
- [ ] `rhi_vulkan` has no RenderGraph dependency.
- [ ] `renderer_basic` remains backend-agnostic.
- [ ] Existing `--smoke-editor-viewport` and `--smoke-editor-viewport-resize` pass after shared-image work.
- [ ] New `--smoke-editor-viewport-native` covers ABI validation, device compatibility, external image render, resize, and packet release.
- [ ] No render loop path calls `vkDeviceWaitIdle`.
- [ ] Encoding, whitespace, managed tests, native builds, and viewport smokes pass.
