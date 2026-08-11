namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeApi
{
    ViewportNativeStatus QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    ViewportNativeStatus OpenStreamV6(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV6 stream);

    ViewportNativeStatus SubmitLatestV6(
        ulong streamId,
        in ViewportNativePresentRequestV6 request);

    ViewportNativeStatus TryTakeReadyV6(
        ulong streamId,
        out ViewportNativeReadyFrameV6 frame);

    void CompleteFrameV6(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind);

    void ReleaseSlotImportV6(ulong streamId, nint nativeSlot);

    void CloseStreamV6(ulong streamId);

    ViewportNativeStatus PollStreamV6(
        ulong streamId,
        out ViewportNativeStreamPollV6 poll);

    void DestroyStreamV6(ulong streamId);

    void Shutdown();
}
