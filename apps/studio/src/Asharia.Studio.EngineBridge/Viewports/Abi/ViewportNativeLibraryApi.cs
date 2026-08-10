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

    public ViewportNativeStatus OpenStreamV5(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV5 stream) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.OpenStreamV5(in compatibility, out stream);

    public ViewportNativeStatus SubmitLatestV5(
        ulong streamId,
        in ViewportNativePresentRequestV5 request) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.SubmitLatestV5(streamId, in request);

    public ViewportNativeStatus TryTakeReadyV5(
        ulong streamId,
        out ViewportNativeReadyFrameV5 frame) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.TryTakeReadyV5(streamId, out frame);

    public void CompleteFrameV5(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind)
    {
        var status = (ViewportNativeStatus)ViewportNativeEntryPoints.CompleteFrameV5(
            streamId,
            nativeSlot,
            (uint)completionKind);
        if (status != ViewportNativeStatus.Success)
        {
            throw new InvalidOperationException(
                $"Native viewport frame completion failed with {status}.");
        }
    }

    public void ReleaseSlotImportV5(ulong streamId, nint nativeSlot) =>
        EnsureSuccess(
            ViewportNativeEntryPoints.ReleaseSlotImportV5(streamId, nativeSlot),
            "slot import release");

    public void CloseStreamV5(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.CloseStreamV5(streamId), "stream close");

    public ViewportNativeStatus PollStreamV5(
        ulong streamId,
        out ViewportNativeStreamPollV5 poll) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.PollStreamV5(streamId, out poll);

    public void DestroyStreamV5(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.DestroyStreamV5(streamId), "stream destroy");

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

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_open_stream_v5")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint OpenStreamV5(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV5 stream);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_submit_latest_v5")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SubmitLatestV5(
        ulong streamId,
        in ViewportNativePresentRequestV5 request);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_try_take_ready_v5")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint TryTakeReadyV5(
        ulong streamId,
        out ViewportNativeReadyFrameV5 frame);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_complete_frame_v5")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CompleteFrameV5(
        ulong streamId,
        nint nativeSlot,
        uint completionKind);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_release_slot_import_v5")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint ReleaseSlotImportV5(ulong streamId, nint nativeSlot);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_close_stream_v5")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CloseStreamV5(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_poll_stream_v5")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint PollStreamV5(
        ulong streamId,
        out ViewportNativeStreamPollV5 poll);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_destroy_stream_v5")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint DestroyStreamV5(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_shutdown")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Shutdown();
}
