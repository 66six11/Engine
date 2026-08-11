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

    public ViewportNativeStatus OpenStreamV7(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV7 stream) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.OpenStreamV7(in compatibility, out stream);

    public ViewportNativeStatus SubmitLatestV7(
        ulong streamId,
        in ViewportNativePresentRequestV7 request) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.SubmitLatestV7(streamId, in request);

    public ViewportNativeStatus TryTakeReadyV7(
        ulong streamId,
        out ViewportNativeReadyFrameV7 frame) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.TryTakeReadyV7(streamId, out frame);

    public void CompleteFrameV7(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind)
    {
        var status = (ViewportNativeStatus)ViewportNativeEntryPoints.CompleteFrameV7(
            streamId,
            nativeSlot,
            (uint)completionKind);
        if (status != ViewportNativeStatus.Success)
        {
            throw new InvalidOperationException(
                $"Native viewport frame completion failed with {status}.");
        }
    }

    public void ReleaseSlotImportV7(ulong streamId, nint nativeSlot) =>
        EnsureSuccess(
            ViewportNativeEntryPoints.ReleaseSlotImportV7(streamId, nativeSlot),
            "slot import release");

    public void CloseStreamV7(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.CloseStreamV7(streamId), "stream close");

    public ViewportNativeStatus PollStreamV7(
        ulong streamId,
        out ViewportNativeStreamPollV7 poll) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.PollStreamV7(streamId, out poll);

    public void DestroyStreamV7(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.DestroyStreamV7(streamId), "stream destroy");

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

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_open_stream_v7")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint OpenStreamV7(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV7 stream);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_submit_latest_v7")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SubmitLatestV7(
        ulong streamId,
        in ViewportNativePresentRequestV7 request);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_try_take_ready_v7")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint TryTakeReadyV7(
        ulong streamId,
        out ViewportNativeReadyFrameV7 frame);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_complete_frame_v7")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CompleteFrameV7(
        ulong streamId,
        nint nativeSlot,
        uint completionKind);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_release_slot_import_v7")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint ReleaseSlotImportV7(ulong streamId, nint nativeSlot);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_close_stream_v7")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CloseStreamV7(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_poll_stream_v7")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint PollStreamV7(
        ulong streamId,
        out ViewportNativeStreamPollV7 poll);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_destroy_stream_v7")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint DestroyStreamV7(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = "editor_viewport_shutdown")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Shutdown();
}
