namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeApi
{
    ViewportNativeStatus CreatePresentSlotV4(
        in ViewportNativePresentRequestV4 request,
        out ViewportNativePresentPacket packet);

    void ReleasePresentPacket(ViewportNativePresentPacket packet);
}
