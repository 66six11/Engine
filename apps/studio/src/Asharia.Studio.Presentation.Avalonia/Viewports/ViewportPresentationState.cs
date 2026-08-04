namespace Asharia.Studio.Presentation.Avalonia.Viewports;

public enum ViewportPresentationState
{
    Detached,
    WaitingForDocument,
    Probing,
    Ready,
    Unsupported,
    NativeUnavailable,
    DeviceMismatch,
    RenderFailed,
    Draining,
}
