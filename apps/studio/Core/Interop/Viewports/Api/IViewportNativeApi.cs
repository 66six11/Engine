namespace Editor.Core.Interop.Viewports.Api;

internal interface IViewportNativeApi
{
    uint QueryCompositionCompatibility(
        in ViewportNativeCompatibilityRequest request,
        ref ViewportNativeCompatibilityResult result);

    void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result);

    uint AcquirePresentPacket(
        in ViewportNativeCompatibilityRequest request,
        ref ViewportNativePresentPacket packet);

    void ReleasePresentPacket(ViewportNativePresentPacket packet);

    void Shutdown();
}
