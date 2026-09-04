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

    public ViewportNativeStatus OpenStreamV9(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV9 stream) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.OpenStreamV9(in compatibility, out stream);

    public ViewportNativeStatus SubmitLatestV9(
        ulong streamId,
        in ViewportNativePresentRequestV9 request) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.SubmitLatestV9(streamId, in request);

    public ViewportNativeStatus TryTakeReadyV9(
        ulong streamId,
        out ViewportNativeReadyFrameV9 frame) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.TryTakeReadyV9(streamId, out frame);

    public void CompleteFrameV9(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind)
    {
        var status = (ViewportNativeStatus)ViewportNativeEntryPoints.CompleteFrameV9(
            streamId,
            nativeSlot,
            (uint)completionKind);
        if (status != ViewportNativeStatus.Success)
        {
            throw new InvalidOperationException(
                $"Native viewport frame completion failed with {status}.");
        }
    }

    public void ReleaseSlotImportV9(ulong streamId, nint nativeSlot) =>
        EnsureSuccess(
            ViewportNativeEntryPoints.ReleaseSlotImportV9(streamId, nativeSlot),
            "slot import release");

    public void CloseStreamV9(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.CloseStreamV9(streamId), "stream close");

    public ViewportNativeStatus PollStreamV9(
        ulong streamId,
        out ViewportNativeStreamPollV9 poll) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.PollStreamV9(streamId, out poll);

    public void DestroyStreamV9(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.DestroyStreamV9(streamId), "stream destroy");

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
    internal const string OpenStreamV9Export = "editor_viewport_open_stream_v9";
    internal const string SubmitLatestV9Export = "editor_viewport_submit_latest_v9";
    internal const string TryTakeReadyV9Export = "editor_viewport_try_take_ready_v9";
    internal const string CompleteFrameV9Export = "editor_viewport_complete_frame_v9";
    internal const string ReleaseSlotImportV9Export =
        "editor_viewport_release_slot_import_v9";
    internal const string CloseStreamV9Export = "editor_viewport_close_stream_v9";
    internal const string PollStreamV9Export = "editor_viewport_poll_stream_v9";
    internal const string DestroyStreamV9Export = "editor_viewport_destroy_stream_v9";
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

    [LibraryImport(LibraryName, EntryPoint = OpenStreamV9Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint OpenStreamV9(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV9 stream);

    [LibraryImport(LibraryName, EntryPoint = SubmitLatestV9Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SubmitLatestV9(
        ulong streamId,
        in ViewportNativePresentRequestV9 request);

    [LibraryImport(LibraryName, EntryPoint = TryTakeReadyV9Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint TryTakeReadyV9(
        ulong streamId,
        out ViewportNativeReadyFrameV9 frame);

    [LibraryImport(LibraryName, EntryPoint = CompleteFrameV9Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CompleteFrameV9(
        ulong streamId,
        nint nativeSlot,
        uint completionKind);

    [LibraryImport(LibraryName, EntryPoint = ReleaseSlotImportV9Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint ReleaseSlotImportV9(ulong streamId, nint nativeSlot);

    [LibraryImport(LibraryName, EntryPoint = CloseStreamV9Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CloseStreamV9(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = PollStreamV9Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint PollStreamV9(
        ulong streamId,
        out ViewportNativeStreamPollV9 poll);

    [LibraryImport(LibraryName, EntryPoint = DestroyStreamV9Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint DestroyStreamV9(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = ShutdownExport)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Shutdown();
}
