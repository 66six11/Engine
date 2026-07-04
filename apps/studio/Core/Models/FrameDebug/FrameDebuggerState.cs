namespace Editor.Core.Models.FrameDebug;

public enum FrameDebuggerState
{
    Unavailable,
    Running,
    CaptureRequested,
    CapturingFrame,
    PausedFrameDebug,
    ResumeRequested,
    Faulted,
}
