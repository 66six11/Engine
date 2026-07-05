namespace Editor.Core.Models.Viewports;

public enum ViewportUpdatePolicy
{
    DirtyOnly,
    InteractiveBurst,
    TimePlayback,
    RuntimePlay,
    FrameDebug,
    PerformancePreview,
}
