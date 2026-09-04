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

    public ViewportNativeStatus OpenStreamV8(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV8 stream) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.OpenStreamV8(in compatibility, out stream);

    public ViewportNativeStatus SubmitLatestV8(
        ulong streamId,
        in ViewportNativePresentRequestV8 request) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.SubmitLatestV8(streamId, in request);

    public ViewportNativeStatus TryTakeReadyV8(
        ulong streamId,
        out ViewportNativeReadyFrameV8 frame) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.TryTakeReadyV8(streamId, out frame);

    public void CompleteFrameV8(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind)
    {
        var status = (ViewportNativeStatus)ViewportNativeEntryPoints.CompleteFrameV8(
            streamId,
            nativeSlot,
            (uint)completionKind);
        if (status != ViewportNativeStatus.Success)
        {
            throw new InvalidOperationException(
                $"Native viewport frame completion failed with {status}.");
        }
    }

    public void ReleaseSlotImportV8(ulong streamId, nint nativeSlot) =>
        EnsureSuccess(
            ViewportNativeEntryPoints.ReleaseSlotImportV8(streamId, nativeSlot),
            "slot import release");

    public void CloseStreamV8(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.CloseStreamV8(streamId), "stream close");

    public ViewportNativeStatus PollStreamV8(
        ulong streamId,
        out ViewportNativeStreamPollV8 poll) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.PollStreamV8(streamId, out poll);

    public void DestroyStreamV8(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.DestroyStreamV8(streamId), "stream destroy");

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
    internal const string OpenStreamV8Export = "editor_viewport_open_stream_v8";
    internal const string SubmitLatestV8Export = "editor_viewport_submit_latest_v8";
    internal const string TryTakeReadyV8Export = "editor_viewport_try_take_ready_v8";
    internal const string CompleteFrameV8Export = "editor_viewport_complete_frame_v8";
    internal const string ReleaseSlotImportV8Export =
        "editor_viewport_release_slot_import_v8";
    internal const string CloseStreamV8Export = "editor_viewport_close_stream_v8";
    internal const string PollStreamV8Export = "editor_viewport_poll_stream_v8";
    internal const string DestroyStreamV8Export = "editor_viewport_destroy_stream_v8";
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

    [LibraryImport(LibraryName, EntryPoint = OpenStreamV8Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint OpenStreamV8(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV8 stream);

    [LibraryImport(LibraryName, EntryPoint = SubmitLatestV8Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SubmitLatestV8(
        ulong streamId,
        in ViewportNativePresentRequestV8 request);

    [LibraryImport(LibraryName, EntryPoint = TryTakeReadyV8Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint TryTakeReadyV8(
        ulong streamId,
        out ViewportNativeReadyFrameV8 frame);

    [LibraryImport(LibraryName, EntryPoint = CompleteFrameV8Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CompleteFrameV8(
        ulong streamId,
        nint nativeSlot,
        uint completionKind);

    [LibraryImport(LibraryName, EntryPoint = ReleaseSlotImportV8Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint ReleaseSlotImportV8(ulong streamId, nint nativeSlot);

    [LibraryImport(LibraryName, EntryPoint = CloseStreamV8Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CloseStreamV8(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = PollStreamV8Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint PollStreamV8(
        ulong streamId,
        out ViewportNativeStreamPollV8 poll);

    [LibraryImport(LibraryName, EntryPoint = DestroyStreamV8Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint DestroyStreamV8(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = ShutdownExport)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Shutdown();
}
