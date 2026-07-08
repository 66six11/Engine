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
