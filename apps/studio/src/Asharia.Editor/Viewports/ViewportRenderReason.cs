using System;

namespace Asharia.Editor.Viewports;

[Flags]
public enum ViewportRenderReason
{
    None = 0,
    InitialFrameMissing = 1 << 0,
    VisibleExposed = 1 << 1,
    Resized = 1 << 2,
    CameraChanged = 1 << 3,
    InputActive = 1 << 4,
    TimeAdvanced = 1 << 5,
    AssetChanged = 1 << 6,
    ShaderChanged = 1 << 7,
    FrameDebugStep = 1 << 8,
    RuntimePlaying = 1 << 9,
    CaptureRequested = 1 << 10,
    All = InitialFrameMissing
        | VisibleExposed
        | Resized
        | CameraChanged
        | InputActive
        | TimeAdvanced
        | AssetChanged
        | ShaderChanged
        | FrameDebugStep
        | RuntimePlaying
        | CaptureRequested,
}
