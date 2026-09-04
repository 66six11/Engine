namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeApi
{
    ViewportNativeStatus QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    ViewportNativeStatus OpenStreamV9(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV9 stream);

    ViewportNativeStatus SubmitLatestV9(
        ulong streamId,
        in ViewportNativePresentRequestV9 request);

    ViewportNativeStatus TryTakeReadyV9(
        ulong streamId,
        out ViewportNativeReadyFrameV9 frame);

    void CompleteFrameV9(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind);

    void ReleaseSlotImportV9(ulong streamId, nint nativeSlot);

    void CloseStreamV9(ulong streamId);

    ViewportNativeStatus PollStreamV9(
        ulong streamId,
        out ViewportNativeStreamPollV9 poll);

    void DestroyStreamV9(ulong streamId);

    void Shutdown();
}
