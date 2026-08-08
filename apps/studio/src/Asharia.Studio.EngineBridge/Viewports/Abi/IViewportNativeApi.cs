namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeApi
{
    ViewportNativeStatus QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    ViewportNativeStatus OpenStreamV5(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV5 stream);

    ViewportNativeStatus SubmitLatestV5(
        ulong streamId,
        in ViewportNativePresentRequestV5 request);

    ViewportNativeStatus TryTakeReadyV5(
        ulong streamId,
        out ViewportNativeReadyFrameV5 frame);

    void CompleteFrameV5(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind);

    void ReleaseSlotImportV5(ulong streamId, nint nativeSlot);

    void CloseStreamV5(ulong streamId);

    ViewportNativeStatus PollStreamV5(
        ulong streamId,
        out ViewportNativeStreamPollV5 poll);

    void DestroyStreamV5(ulong streamId);

    void Shutdown();
}
