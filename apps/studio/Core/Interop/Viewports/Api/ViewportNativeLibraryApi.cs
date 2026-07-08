using System.Runtime.InteropServices;

namespace Editor.Core.Interop.Viewports.Api;

internal sealed class ViewportNativeLibraryApi : IViewportNativeApi
{
    public static ViewportNativeLibraryApi Instance { get; } = new();

    private ViewportNativeLibraryApi()
    {
    }

    public uint QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        ref ViewportNativeCompatibilityResult result)
    {
        return ViewportNativeEntryPoints.QueryCompositionCompatibility(request, ref result);
    }

    public void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result)
    {
        ViewportNativeEntryPoints.ReleaseCompatibilityResult(result);
    }

    public uint AcquirePresentPacket(
        in ViewportNativePresentRequest request,
        ref ViewportNativePresentPacket packet)
    {
        return ViewportNativeEntryPoints.AcquirePresentPacket(request, ref packet);
    }

    public void ReleasePresentPacket(ViewportNativePresentPacket packet)
    {
        ViewportNativeEntryPoints.ReleasePresentPacket(packet);
    }

    public void Shutdown()
    {
        ViewportNativeEntryPoints.Shutdown();
    }
}

internal static partial class ViewportNativeEntryPoints
{
    private const string LibraryName = "editor_native";

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_query_composition_compatibility")]
    internal static partial uint QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        ref ViewportNativeCompatibilityResult result);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_release_compatibility_result")]
    internal static partial void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_acquire_present_packet")]
    internal static partial uint AcquirePresentPacket(
        in ViewportNativePresentRequest request,
        ref ViewportNativePresentPacket packet);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_release_present_packet")]
    internal static partial void ReleasePresentPacket(ViewportNativePresentPacket packet);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_shutdown")]
    internal static partial void Shutdown();
}
