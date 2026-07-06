namespace Editor.Core.Models.Viewports;

public enum ViewportCompositionStatus
{
    Unknown,
    NoTopLevel,
    NoCompositor,
    GpuInteropUnavailable,
    GpuInteropLost,
    VulkanOpaqueNtUnsupported,
    SemaphoreUnsupported,
    Supported,
}
