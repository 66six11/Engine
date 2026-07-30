namespace Editor.Core.Interop.Viewports.Api;

internal interface IViewportNativeApi
{
    uint QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        ref ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    uint AcquirePresentPacketV2(
        in ViewportNativePresentRequestV2 request,
        ref ViewportNativePresentPacket packet);

    uint CreatePresentSlotV3(
        in ViewportNativePresentRequestV2 request,
        ref ViewportNativePresentPacket packet);

    uint RenderPresentSlotV3(
        in ViewportNativePresentSlotRenderRequest request,
        ref ViewportNativePresentPacket packet);

    void ReleasePresentPacket(ViewportNativePresentPacket packet);

    void Shutdown();
}
