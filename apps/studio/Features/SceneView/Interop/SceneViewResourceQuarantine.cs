using System;
using System.Collections.Generic;
using Avalonia.Rendering.Composition;
using Editor.Core.Interop.Viewports.Adapters;
using Editor.Core.Interop.Viewports.Api;

namespace Editor.Features.SceneView.Interop;

internal static class SceneViewResourceQuarantine
{
    private static readonly object Gate = new();
    private static readonly List<QuarantinedResource> Resources = [];

    public static int Count
    {
        get
        {
            lock (Gate)
            {
                return Resources.Count;
            }
        }
    }

    public static void Retain(
        ICompositionImportedGpuImage? image,
        ICompositionImportedGpuSemaphore? waitSemaphore,
        ICompositionImportedGpuSemaphore? signalSemaphore,
        ViewportNativePresentPacket packet,
        Exception failure)
    {
        ArgumentNullException.ThrowIfNull(failure);

        lock (Gate)
        {
            Resources.Add(
                new QuarantinedResource(
                    image,
                    waitSemaphore,
                    signalSemaphore,
                    packet,
                    failure));
        }

        ViewportNativePresentDrain.RequestProcessExitFallback();
    }

    private sealed record QuarantinedResource(
        ICompositionImportedGpuImage? Image,
        ICompositionImportedGpuSemaphore? WaitSemaphore,
        ICompositionImportedGpuSemaphore? SignalSemaphore,
        ViewportNativePresentPacket Packet,
        Exception Failure);
}
