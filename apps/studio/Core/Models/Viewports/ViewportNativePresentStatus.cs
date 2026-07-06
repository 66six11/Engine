namespace Editor.Core.Models.Viewports;

public enum ViewportNativePresentStatus
{
    Success,
    RenderProducerUnavailable,
    UnsupportedAbi,
    UnsupportedCompositionInterop,
    DeviceMismatch,
    UnsupportedHandleType,
    RenderFailed,
    ImportFailed,
    DeviceLost,
}
