using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal sealed class ViewportNativeLibraryApi : IViewportNativeApi
{
    public static ViewportNativeLibraryApi Instance { get; } = new();

    private ViewportNativeLibraryApi()
    {
    }

    public ViewportNativeStatus CreatePresentSlotV4(
        in ViewportNativePresentRequestV4 request,
        out ViewportNativePresentPacket packet) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.CreatePresentSlotV4(in request, out packet);

    public void ReleasePresentPacket(ViewportNativePresentPacket packet) =>
        ViewportNativeEntryPoints.ReleasePresentPacket(packet);
}

internal static partial class ViewportNativeEntryPoints
{
    private const string LibraryName = "editor_native";

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_create_present_slot_v4")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CreatePresentSlotV4(
        in ViewportNativePresentRequestV4 request,
        out ViewportNativePresentPacket packet);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_release_present_packet")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void ReleasePresentPacket(ViewportNativePresentPacket packet);
}
