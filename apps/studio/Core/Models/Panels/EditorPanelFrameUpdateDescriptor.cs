using Asharia.Editor.Panels;
namespace Editor.Core.Models.Panels;

public sealed record EditorPanelFrameUpdateDescriptor
{
    public static EditorPanelFrameUpdateDescriptor Manual { get; } =
        new(EditorPanelFrameUpdateMode.Manual);

    public EditorPanelFrameUpdateDescriptor(
        EditorPanelFrameUpdateMode mode,
        double? targetFramesPerSecond = null)
    {
        Mode = mode;
        TargetFramesPerSecond = targetFramesPerSecond;
    }

    public EditorPanelFrameUpdateMode Mode { get; init; }

    public double? TargetFramesPerSecond { get; init; }
}
