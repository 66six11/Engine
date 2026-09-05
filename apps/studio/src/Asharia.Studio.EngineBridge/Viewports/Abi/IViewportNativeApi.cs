namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeApi
{
    ViewportNativeStatus QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    ViewportNativeStatus OpenStreamV10(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV10 stream);

    ViewportNativeStatus SubmitLatestV10(
        ulong streamId,
        in ViewportNativePresentRequestV10 request);

    ViewportNativeStatus TryTakeReadyV10(
        ulong streamId,
        out ViewportNativeReadyFrameV10 frame);

    void CompleteFrameV10(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind);

    void ReleaseSlotImportV10(ulong streamId, nint nativeSlot);

    void CloseStreamV10(ulong streamId);

    ViewportNativeStatus PollStreamV10(
        ulong streamId,
        out ViewportNativeStreamPollV10 poll);

    void DestroyStreamV10(ulong streamId);

    void Shutdown();
}
