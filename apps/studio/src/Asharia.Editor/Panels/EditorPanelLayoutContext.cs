using System;

namespace Asharia.Editor.Panels;

public sealed record EditorPanelLayoutContext
{
    public EditorPanelLayoutContext(
        EditorPanelLifecycleContext panel,
        double logicalWidth,
        double logicalHeight,
        double renderScale)
    {
        ArgumentNullException.ThrowIfNull(panel);

        if (!double.IsFinite(logicalWidth) || logicalWidth < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(logicalWidth),
                logicalWidth,
                "Logical width must be finite and non-negative.");
        }

        if (!double.IsFinite(logicalHeight) || logicalHeight < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(logicalHeight),
                logicalHeight,
                "Logical height must be finite and non-negative.");
        }

        if (!double.IsFinite(renderScale) || renderScale <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(renderScale),
                renderScale,
                "Render scale must be finite and positive.");
        }

        Panel = panel;
        LogicalWidth = logicalWidth;
        LogicalHeight = logicalHeight;
        RenderScale = renderScale;
    }

    public EditorPanelLifecycleContext Panel { get; }

    public double LogicalWidth { get; }

    public double LogicalHeight { get; }

    public double RenderScale { get; }

    public bool HasPositiveArea => LogicalWidth > 0 && LogicalHeight > 0;
}
