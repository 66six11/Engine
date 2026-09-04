namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeApi
{
    ViewportNativeStatus QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        out ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    ViewportNativeStatus OpenStreamV8(
        in ViewportNativeCompatibilityRequest compatibility,
        out ViewportNativeStreamHandleV8 stream);

    ViewportNativeStatus SubmitLatestV8(
        ulong streamId,
        in ViewportNativePresentRequestV8 request);

    ViewportNativeStatus TryTakeReadyV8(
        ulong streamId,
        out ViewportNativeReadyFrameV8 frame);

    void CompleteFrameV8(
        ulong streamId,
        nint nativeSlot,
        ViewportNativePresentCompletionKind completionKind);

    void ReleaseSlotImportV8(ulong streamId, nint nativeSlot);

    void CloseStreamV8(ulong streamId);

    ViewportNativeStatus PollStreamV8(
        ulong streamId,
        out ViewportNativeStreamPollV8 poll);

    void DestroyStreamV8(ulong streamId);

    void Shutdown();
}
