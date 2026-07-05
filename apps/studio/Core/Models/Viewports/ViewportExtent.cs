using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportExtent
{
    public ViewportExtent(
        int widthPixels,
        int heightPixels,
        double renderScale)
    {
        if (widthPixels <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(widthPixels),
                widthPixels,
                "Viewport width in pixels must be greater than zero.");
        }

        if (heightPixels <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(heightPixels),
                heightPixels,
                "Viewport height in pixels must be greater than zero.");
        }

        if (renderScale <= 0 || !double.IsFinite(renderScale))
        {
            throw new ArgumentOutOfRangeException(
                nameof(renderScale),
                renderScale,
                "Viewport render scale must be finite and greater than zero.");
        }

        WidthPixels = widthPixels;
        HeightPixels = heightPixels;
        RenderScale = renderScale;
    }

    public int WidthPixels { get; }

    public int HeightPixels { get; }

    public double RenderScale { get; }
}
