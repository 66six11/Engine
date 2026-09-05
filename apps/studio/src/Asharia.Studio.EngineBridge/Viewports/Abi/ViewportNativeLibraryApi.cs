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

    public ViewportNativeStatus OpenStreamV10(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV10 stream) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.OpenStreamV10(in compatibility, out stream);

    public ViewportNativeStatus SubmitLatestV10(
        ulong streamId,
        in ViewportNativePresentRequestV10 request) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.SubmitLatestV10(streamId, in request);

    public ViewportNativeStatus TryTakeReadyV10(
        ulong streamId,
        out ViewportNativeReadyFrameV10 frame) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.TryTakeReadyV10(streamId, out frame);

    public void CompleteFrameV10(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind)
    {
        var status = (ViewportNativeStatus)ViewportNativeEntryPoints.CompleteFrameV10(
            streamId,
            nativeSlot,
            (uint)completionKind);
        if (status != ViewportNativeStatus.Success)
        {
            throw new InvalidOperationException(
                $"Native viewport frame completion failed with {status}.");
        }
    }

    public void ReleaseSlotImportV10(ulong streamId, nint nativeSlot) =>
        EnsureSuccess(
            ViewportNativeEntryPoints.ReleaseSlotImportV10(streamId, nativeSlot),
            "slot import release");

    public void CloseStreamV10(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.CloseStreamV10(streamId), "stream close");

    public ViewportNativeStatus PollStreamV10(
        ulong streamId,
        out ViewportNativeStreamPollV10 poll) =>
        (ViewportNativeStatus)ViewportNativeEntryPoints.PollStreamV10(streamId, out poll);

    public void DestroyStreamV10(ulong streamId) =>
        EnsureSuccess(ViewportNativeEntryPoints.DestroyStreamV10(streamId), "stream destroy");

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
    internal const string OpenStreamV10Export = "editor_viewport_open_stream_v10";
    internal const string SubmitLatestV10Export = "editor_viewport_submit_latest_v10";
    internal const string TryTakeReadyV10Export = "editor_viewport_try_take_ready_v10";
    internal const string CompleteFrameV10Export = "editor_viewport_complete_frame_v10";
    internal const string ReleaseSlotImportV10Export =
        "editor_viewport_release_slot_import_v10";
    internal const string CloseStreamV10Export = "editor_viewport_close_stream_v10";
    internal const string PollStreamV10Export = "editor_viewport_poll_stream_v10";
    internal const string DestroyStreamV10Export = "editor_viewport_destroy_stream_v10";
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

    [LibraryImport(LibraryName, EntryPoint = OpenStreamV10Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint OpenStreamV10(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV10 stream);

    [LibraryImport(LibraryName, EntryPoint = SubmitLatestV10Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint SubmitLatestV10(
        ulong streamId,
        in ViewportNativePresentRequestV10 request);

    [LibraryImport(LibraryName, EntryPoint = TryTakeReadyV10Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint TryTakeReadyV10(
        ulong streamId,
        out ViewportNativeReadyFrameV10 frame);

    [LibraryImport(LibraryName, EntryPoint = CompleteFrameV10Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CompleteFrameV10(
        ulong streamId,
        nint nativeSlot,
        uint completionKind);

    [LibraryImport(LibraryName, EntryPoint = ReleaseSlotImportV10Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint ReleaseSlotImportV10(ulong streamId, nint nativeSlot);

    [LibraryImport(LibraryName, EntryPoint = CloseStreamV10Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint CloseStreamV10(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = PollStreamV10Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint PollStreamV10(
        ulong streamId,
        out ViewportNativeStreamPollV10 poll);

    [LibraryImport(LibraryName, EntryPoint = DestroyStreamV10Export)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint DestroyStreamV10(ulong streamId);

    [LibraryImport(LibraryName, EntryPoint = ShutdownExport)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Shutdown();
}
