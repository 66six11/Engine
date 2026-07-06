using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
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

        if (TopLevel.GetTopLevel(host) is null)
        {
            return Snapshot(
                viewportId,
                ViewportCompositionStatus.NoTopLevel,
                deviceLuid: null,
                deviceUuid: null,
                imageTypes: [],
                semaphoreTypes: [],
                syncCapabilities: [],
                "Scene View is not attached to a TopLevel.");
        }

        var compositor = ElementComposition.GetElementVisual(host)?.Compositor
            ?? Compositor.TryGetDefaultCompositor();
        if (compositor is null)
        {
            return Snapshot(
                viewportId,
                ViewportCompositionStatus.NoCompositor,
                deviceLuid: null,
                deviceUuid: null,
                imageTypes: [],
                semaphoreTypes: [],
                syncCapabilities: [],
                "Avalonia compositor is unavailable.");
        }

        var interop = await compositor.TryGetCompositionGpuInterop();
        if (interop is null)
        {
            return Snapshot(
                viewportId,
                ViewportCompositionStatus.GpuInteropUnavailable,
                deviceLuid: null,
                deviceUuid: null,
                imageTypes: [],
                semaphoreTypes: [],
                syncCapabilities: [],
                "Avalonia composition GPU interop is unavailable.");
        }

        var imageTypes = interop.SupportedImageHandleTypes.ToArray();
        var semaphoreTypes = interop.SupportedSemaphoreTypes.ToArray();
        var syncCapabilities = interop
            .GetSynchronizationCapabilities(VulkanImageHandleType)
            .ToString()
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        var status = GetStatus(interop, imageTypes, semaphoreTypes);

        return Snapshot(
            viewportId,
            status,
            FormatDeviceId(interop.DeviceLuid),
            FormatDeviceId(interop.DeviceUuid),
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

    private static string? FormatDeviceId(byte[]? value)
    {
        return value is { Length: > 0 }
            ? Convert.ToHexString(value).ToLowerInvariant()
            : null;
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
