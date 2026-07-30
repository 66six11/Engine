using System;
using System.Runtime.InteropServices;

namespace Editor.Core.Interop.Viewports.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativePresentRequest
{
    public static uint CurrentStructSize => checked((uint)Marshal.SizeOf<ViewportNativePresentRequest>());

    public ViewportNativePresentRequest(
        ViewportNativeCompatibilityRequest compatibility,
        uint widthPixels,
        uint heightPixels)
    {
        Header = new ViewportNativeAbiHeader(CurrentStructSize);
        Compatibility = compatibility;
        WidthPixels = widthPixels;
        HeightPixels = heightPixels;
    }

    public ViewportNativeAbiHeader Header { get; }

    public ViewportNativeCompatibilityRequest Compatibility { get; }

    public uint WidthPixels { get; }

    public uint HeightPixels { get; }
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativePresentRequestV2
{
    public static uint CurrentStructSize =>
        checked((uint)Marshal.SizeOf<ViewportNativePresentRequestV2>());

    public ViewportNativePresentRequestV2(
        ViewportNativeCompatibilityRequest compatibility,
        uint widthPixels,
        uint heightPixels,
        bool hasScene,
        ulong sceneRevision)
    {
        Header = new ViewportNativeAbiHeader(CurrentStructSize);
        Compatibility = compatibility;
        WidthPixels = widthPixels;
        HeightPixels = heightPixels;
        HasScene = hasScene ? 1U : 0U;
        Reserved = 0U;
        SceneRevision = hasScene ? sceneRevision : 0UL;
    }

    public ViewportNativeAbiHeader Header { get; }

    public ViewportNativeCompatibilityRequest Compatibility { get; }

    public uint WidthPixels { get; }

    public uint HeightPixels { get; }

    public uint HasScene { get; }

    public uint Reserved { get; }

    public ulong SceneRevision { get; }
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativePresentSlotRenderRequest
{
    public static uint CurrentStructSize =>
        checked((uint)Marshal.SizeOf<ViewportNativePresentSlotRenderRequest>());

    public ViewportNativePresentSlotRenderRequest(
        IntPtr nativeSlot,
        uint widthPixels,
        uint heightPixels,
        bool hasScene,
        ulong sceneRevision)
    {
        Header = new ViewportNativeAbiHeader(CurrentStructSize);
        NativeSlot = nativeSlot;
        WidthPixels = widthPixels;
        HeightPixels = heightPixels;
        HasScene = hasScene ? 1U : 0U;
        Reserved = 0U;
        SceneRevision = hasScene ? sceneRevision : 0UL;
    }

    public ViewportNativeAbiHeader Header { get; }

    public IntPtr NativeSlot { get; }

    public uint WidthPixels { get; }

    public uint HeightPixels { get; }

    public uint HasScene { get; }

    public uint Reserved { get; }

    public ulong SceneRevision { get; }
}
