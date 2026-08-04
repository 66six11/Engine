using Avalonia;

namespace Editor.Shell.Docking.TabStrips;

internal static class EditorDockTabStripScrollController
{
    public const double DefaultRevealPadding = 8.0;
    public const double DefaultEdgeAutoScrollZone = 24.0;
    public const double DefaultEdgeAutoScrollStep = 48.0;

    public static double ClampOffset(double requestedOffset, double extentWidth, double viewportWidth)
    {
        var maxOffset = GetMaxOffset(extentWidth, viewportWidth);
        if (maxOffset <= 0)
        {
            return 0;
        }

        if (double.IsNaN(requestedOffset) || double.IsInfinity(requestedOffset))
        {
            return 0;
        }

        return System.Math.Clamp(requestedOffset, 0, maxOffset);
    }

    public static double CalculateOffsetToReveal(
        Rect targetBounds,
        double currentOffset,
        double extentWidth,
        double viewportWidth,
        double padding = DefaultRevealPadding)
    {
        var normalizedOffset = ClampOffset(currentOffset, extentWidth, viewportWidth);
        if (targetBounds.Width <= 0 || viewportWidth <= 0 || extentWidth <= viewportWidth)
        {
            return normalizedOffset;
        }

        var normalizedPadding = NormalizeNonNegative(padding);
        var leftEdge = normalizedOffset + normalizedPadding;
        var rightEdge = normalizedOffset + viewportWidth - normalizedPadding;
        if (targetBounds.X < leftEdge)
        {
            return ClampOffset(targetBounds.X - normalizedPadding, extentWidth, viewportWidth);
        }

        if (targetBounds.Right > rightEdge)
        {
            return ClampOffset(
                targetBounds.Right - viewportWidth + normalizedPadding,
                extentWidth,
                viewportWidth);
        }

        return normalizedOffset;
    }

    public static double CalculateAutoScrollOffset(
        double pointerX,
        double currentOffset,
        double extentWidth,
        double viewportWidth,
        double edgeZone = DefaultEdgeAutoScrollZone,
        double step = DefaultEdgeAutoScrollStep)
    {
        var normalizedOffset = ClampOffset(currentOffset, extentWidth, viewportWidth);
        if (viewportWidth <= 0 || extentWidth <= viewportWidth)
        {
            return 0;
        }

        var normalizedZone = NormalizeNonNegative(edgeZone);
        var normalizedStep = NormalizeNonNegative(step);
        if (normalizedZone <= 0 || normalizedStep <= 0)
        {
            return normalizedOffset;
        }

        var isInLeftZone = pointerX <= normalizedZone;
        var isInRightZone = pointerX >= viewportWidth - normalizedZone;
        if (isInLeftZone && isInRightZone)
        {
            var distanceToLeftEdge = pointerX;
            var distanceToRightEdge = viewportWidth - pointerX;
            return distanceToLeftEdge <= distanceToRightEdge
                ? ClampOffset(normalizedOffset - normalizedStep, extentWidth, viewportWidth)
                : ClampOffset(normalizedOffset + normalizedStep, extentWidth, viewportWidth);
        }

        if (isInLeftZone)
        {
            return ClampOffset(normalizedOffset - normalizedStep, extentWidth, viewportWidth);
        }

        if (isInRightZone)
        {
            return ClampOffset(normalizedOffset + normalizedStep, extentWidth, viewportWidth);
        }

        return normalizedOffset;
    }

    public static double CalculateContentOriginX(double viewportOriginX, double horizontalOffset)
    {
        if (double.IsNaN(viewportOriginX) || double.IsInfinity(viewportOriginX))
        {
            return 0;
        }

        if (double.IsNaN(horizontalOffset) || double.IsInfinity(horizontalOffset))
        {
            return viewportOriginX;
        }

        return viewportOriginX - horizontalOffset;
    }

    private static double GetMaxOffset(double extentWidth, double viewportWidth)
    {
        if (double.IsNaN(extentWidth)
            || double.IsInfinity(extentWidth)
            || double.IsNaN(viewportWidth)
            || double.IsInfinity(viewportWidth)
            || extentWidth <= 0
            || viewportWidth <= 0)
        {
            return 0;
        }

        return System.Math.Max(0, extentWidth - viewportWidth);
    }

    private static double NormalizeNonNegative(double value)
    {
        return double.IsNaN(value) || double.IsInfinity(value) || value < 0 ? 0 : value;
    }
}
