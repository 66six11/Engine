using System;

namespace Asharia.Editor.Panels;

public sealed record EditorPanelFrameUpdateRequest
{
    public EditorPanelFrameUpdateRequest(
        EditorPanelFrameUpdateMode mode,
        double? targetFramesPerSecond = null)
    {
        if (!Enum.IsDefined(mode))
        {
            throw new ArgumentOutOfRangeException(
                nameof(mode),
                mode,
                "Editor panel frame update mode is not defined.");
        }

        if (targetFramesPerSecond is { } fps
            && (fps <= 0 || !double.IsFinite(fps)))
        {
            throw new ArgumentOutOfRangeException(
                nameof(targetFramesPerSecond),
                targetFramesPerSecond,
                "Target frames per second must be greater than zero.");
        }

        Mode = mode;
        TargetFramesPerSecond = targetFramesPerSecond;
    }

    public EditorPanelFrameUpdateMode Mode { get; }

    public double? TargetFramesPerSecond { get; }

    public static EditorPanelFrameUpdateRequest Manual { get; } =
        new(EditorPanelFrameUpdateMode.Manual);

    public static EditorPanelFrameUpdateRequest Visible(double? targetFramesPerSecond = null)
    {
        return new EditorPanelFrameUpdateRequest(
            EditorPanelFrameUpdateMode.Visible,
            targetFramesPerSecond);
    }

    public static EditorPanelFrameUpdateRequest Active(double? targetFramesPerSecond = null)
    {
        return new EditorPanelFrameUpdateRequest(
            EditorPanelFrameUpdateMode.Active,
            targetFramesPerSecond);
    }
}
