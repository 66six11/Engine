namespace Asharia.Editor.Diagnostics.FrameDebug;

public enum FrameDebuggerState
{
    Unavailable,
    Running,
    CaptureRequested,
    CapturingFrame,
    WaitingGpuFence,
    PausedFrameDebug,
    ResumeRequested,
    Faulted,
}
