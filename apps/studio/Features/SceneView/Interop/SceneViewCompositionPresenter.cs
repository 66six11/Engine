using System;
using System.Threading.Tasks;
using Asharia.Editor.Viewports;
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
        ArgumentNullException.ThrowIfNull(bridge);

        bridge_ = bridge;
    }

    public async Task<ViewportNativePresentSnapshot> PresentAsync(
        ICompositionGpuInterop interop,
        CompositionDrawingSurface surface,
        ViewportId viewportId,
        ViewportExtent requestedExtent,
        ViewportNativePresentPacket packet)
    {
        ArgumentNullException.ThrowIfNull(interop);
        ArgumentNullException.ThrowIfNull(surface);
        ArgumentNullException.ThrowIfNull(requestedExtent);

        try
        {
            var imageHandle = new PlatformHandle(packet.ImageHandle, ImageHandleType);
            var waitHandle = new PlatformHandle(packet.WaitSemaphoreHandle, SemaphoreHandleType);
            var signalHandle = new PlatformHandle(packet.SignalSemaphoreHandle, SemaphoreHandleType);

            var imageProperties = packet.CreateAvaloniaImageProperties();
            await using var image = interop.ImportImage(imageHandle, imageProperties);
            await using var waitSemaphore = interop.ImportSemaphore(waitHandle);
            await using var signalSemaphore = interop.ImportSemaphore(signalHandle);

            await surface.UpdateWithSemaphoresAsync(image, waitSemaphore, signalSemaphore);

            return packet.ToSnapshot(
                viewportId,
                requestedExtent,
                ViewportNativePresentStatus.Success,
                "Presented native Vulkan viewport frame.");
        }
        catch (Exception ex) when (ex is InvalidOperationException or NotSupportedException)
        {
            return packet.ToSnapshot(
                viewportId,
                requestedExtent,
                ViewportNativePresentStatus.ImportFailed,
                ex.Message);
        }
        finally
        {
            bridge_.ReleasePresentPacket(packet);
        }
    }
}
