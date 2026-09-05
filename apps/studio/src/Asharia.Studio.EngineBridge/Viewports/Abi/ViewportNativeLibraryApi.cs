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

    public ViewportNativeStatus OpenStreamV11(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV11 stream) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.OpenStreamV11(in compatibility, out stream);

    public ViewportNativeStatus SubmitLatestV11(
        ulong streamId,
        in ViewportNativePresentRequestV11 request) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.SubmitLatestV11(streamId, in request);

    public ViewportNativeStatus TryTakeReadyV11(
        ulong streamId,
        out ViewportNativeReadyFrameV11 frame) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.TryTakeReadyV11(streamId, out frame);

    public void CompleteFrameV11(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind)
    {
        var status = (ViewportNativeStatus)ViewportNativeEntryPoints.CompleteFrameV11(
            streamId,
            nativeSlot,
            (uint)completionKind);
        if (status != ViewportNativeStatus.Success)
        {
            throw new InvalidOperationException(
                $"Native viewport frame completion failed with {status}.");
        }
    }

    public void ReleaseSlotImportV11(ulong streamId, nint nativeSlot) =>
        EnsureSuccess(
            ViewportNativeEntryPoints.ReleaseSlotImportV11(streamId, nativeSlot),
            "slot import release");

    public void CloseStreamV11(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.CloseStreamV11(streamId), "stream close");

    public ViewportNativeStatus PollStreamV11(
        ulong streamId,
        out ViewportNativeStreamPollV11 poll) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.PollStreamV11(streamId, out poll);

    public void DestroyStreamV11(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.DestroyStreamV11(streamId), "stream destroy");

    public ViewportNativeStatus WaitStreamChangeV11(ulong streamId, ulong observedRevision, uint timeoutMs) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.WaitStreamChangeV11(streamId, observedRevision, timeoutMs);

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

    internal const string QueryCompositionCompatibilityExport =
        "editor_viewport_query_composition_compatibility";
    internal const string ReleaseCompatibilityResultExport =
        "editor_viewport_release_compatibility_result";
    internal const string OpenStreamV11Export = "editor_viewport_open_stream_v11";
    internal const string SubmitLatestV11Export = "editor_viewport_submit_latest_v11";
    internal const string TryTakeReadyV11Export = "editor_viewport_try_take_ready_v11";
    internal const string CompleteFrameV11Export = "editor_viewport_complete_frame_v11";
    internal const string ReleaseSlotImportV11Export =
        "editor_viewport_release_slot_import_v11";
    internal const string CloseStreamV11Export = "editor_viewport_close_stream_v11";
    internal const string PollStreamV11Export = "editor_viewport_poll_stream_v11";
    internal const string DestroyStreamV11Export = "editor_viewport_destroy_stream_v11";
    internal const string WaitStreamChangeV11Export = "editor_viewport_wait_stream_change_v11";
    internal const string ShutdownExport = "editor_viewport_shutdown";

    [LibraryImport(LibraryName, EntryPoint = QueryCompositionCompatibilityExport)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    [LibraryImport(LibraryName, EntryPoint = ReleaseCompatibilityResultExport)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void ReleaseCompatibilityResult(
        ViewportNativeCompatibilityResult result);

    [LibraryImport(LibraryName, EntryPoint = OpenStreamV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint OpenStreamV11(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV11 stream);

    [LibraryImport(LibraryName, EntryPoint = SubmitLatestV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SubmitLatestV11(
        ulong streamId,
        in ViewportNativePresentRequestV11 request);

    [LibraryImport(LibraryName, EntryPoint = TryTakeReadyV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint TryTakeReadyV11(
        ulong streamId,
        out ViewportNativeReadyFrameV11 frame);

    [LibraryImport(LibraryName, EntryPoint = CompleteFrameV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CompleteFrameV11(
        ulong streamId,
        nint nativeSlot,
        uint completionKind);

    [LibraryImport(LibraryName, EntryPoint = ReleaseSlotImportV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint ReleaseSlotImportV11(ulong streamId, nint nativeSlot);

    [LibraryImport(LibraryName, EntryPoint = CloseStreamV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CloseStreamV11(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = PollStreamV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint PollStreamV11(
        ulong streamId,
        out ViewportNativeStreamPollV11 poll);

    [LibraryImport(LibraryName, EntryPoint = DestroyStreamV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint DestroyStreamV11(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = WaitStreamChangeV11Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint WaitStreamChangeV11(ulong streamId, ulong observedRevision, uint timeoutMs);

    [LibraryImport(LibraryName, EntryPoint = ShutdownExport)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Shutdown();
}
