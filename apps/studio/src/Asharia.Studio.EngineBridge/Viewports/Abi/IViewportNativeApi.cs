namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeApi
{
    ViewportNativeStatus QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    ViewportNativeStatus OpenStreamV7(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV7 stream);

    ViewportNativeStatus SubmitLatestV7(
        ulong streamId,
        in ViewportNativePresentRequestV7 request);

    ViewportNativeStatus TryTakeReadyV7(
        ulong streamId,
        out ViewportNativeReadyFrameV7 frame);

    void CompleteFrameV7(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind);

    void ReleaseSlotImportV7(ulong streamId, nint nativeSlot);

    void CloseStreamV7(ulong streamId);

    ViewportNativeStatus PollStreamV7(
        ulong streamId,
        out ViewportNativeStreamPollV7 poll);

    void DestroyStreamV7(ulong streamId);

    void Shutdown();
}
