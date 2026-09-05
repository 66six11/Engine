namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeApi
{
    ViewportNativeStatus QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    ViewportNativeStatus OpenStreamV11(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV11 stream);

    ViewportNativeStatus SubmitLatestV11(
        ulong streamId,
        in ViewportNativePresentRequestV11 request);

    ViewportNativeStatus TryTakeReadyV11(
        ulong streamId,
        out ViewportNativeReadyFrameV11 frame);

    void CompleteFrameV11(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind);

    void ReleaseSlotImportV11(ulong streamId, nint nativeSlot);

    void CloseStreamV11(ulong streamId);

    ViewportNativeStatus PollStreamV11(
        ulong streamId,
        out ViewportNativeStreamPollV11 poll);

    void DestroyStreamV11(ulong streamId);

    ViewportNativeStatus WaitStreamChangeV11(ulong streamId, ulong observedRevision, uint timeoutMs);

    void Shutdown();
}
