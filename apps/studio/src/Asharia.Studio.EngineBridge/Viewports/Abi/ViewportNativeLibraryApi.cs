using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal sealed class ViewportNativeLibraryApi : IViewportNativeApi
{
    public static ViewportNativeLibraryApi Instance { get; } = new();

    private ViewportNativeLibraryApi()
    {
    }

    public ViewportNativeStatus QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.QueryCompositionCompatibility(
            in request,
            out result);

    public void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result) =>
        ViewportNativeEntryPoints.ReleaseCompatibilityResult(result);

    public ViewportNativeStatus OpenStreamV6(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV6 stream) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.OpenStreamV6(in compatibility, out stream);

    public ViewportNativeStatus SubmitLatestV6(
        ulong streamId,
        in ViewportNativePresentRequestV6 request) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.SubmitLatestV6(streamId, in request);

    public ViewportNativeStatus TryTakeReadyV6(
        ulong streamId,
        out ViewportNativeReadyFrameV6 frame) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.TryTakeReadyV6(streamId, out frame);

    public void CompleteFrameV6(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind)
    {
        var status = (ViewportNativeStatus)ViewportNativeEntryPoints.CompleteFrameV6(
            streamId,
            nativeSlot,
            (uint)completionKind);
        if (status != ViewportNativeStatus.Success)
        {
            throw new InvalidOperationException(
                $"Native viewport frame completion failed with {status}.");
        }
    }

    public void ReleaseSlotImportV6(ulong streamId, nint nativeSlot) =>
        EnsureSuccess(
            ViewportNativeEntryPoints.ReleaseSlotImportV6(streamId, nativeSlot),
            "slot import release");

    public void CloseStreamV6(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.CloseStreamV6(streamId), "stream close");

    public ViewportNativeStatus PollStreamV6(
        ulong streamId,
        out ViewportNativeStreamPollV6 poll) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.PollStreamV6(streamId, out poll);

    public void DestroyStreamV6(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.DestroyStreamV6(streamId), "stream destroy");

    public void Shutdown() => ViewportNativeEntryPoints.Shutdown();

    private static void EnsureSuccess(uint rawStatus, string operation)
    {
        var status = (ViewportNativeStatus)rawStatus;
        if (status != ViewportNativeStatus.Success)
        {
            throw new InvalidOperationException(
                $"Native viewport {operation} failed with {status}.");
        }
    }
}

internal static partial class ViewportNativeEntryPoints
{
    private const string LibraryName = "editor_native";

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_query_composition_compatibility")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_release_compatibility_result")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void ReleaseCompatibilityResult(
        ViewportNativeCompatibilityResult result);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_open_stream_v6")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint OpenStreamV6(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV6 stream);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_submit_latest_v6")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SubmitLatestV6(
        ulong streamId,
        in ViewportNativePresentRequestV6 request);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_try_take_ready_v6")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint TryTakeReadyV6(
        ulong streamId,
        out ViewportNativeReadyFrameV6 frame);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_complete_frame_v6")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CompleteFrameV6(
        ulong streamId,
        nint nativeSlot,
        uint completionKind);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_release_slot_import_v6")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint ReleaseSlotImportV6(ulong streamId, nint nativeSlot);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_close_stream_v6")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CloseStreamV6(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_poll_stream_v6")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint PollStreamV6(
        ulong streamId,
        out ViewportNativeStreamPollV6 poll);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_destroy_stream_v6")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint DestroyStreamV6(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_shutdown")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Shutdown();
}
