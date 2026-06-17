using System;
using Avalonia;
using Avalonia.Controls;

namespace Editor.Shell.Views.Windowing;

internal static class EditorDockFloatingWindowPlacement
{
    public const double MinWidth = 240.0;
    public const double MinHeight = 180.0;

    public static Rect NormalizeBounds(Rect bounds)
    {
        var x = IsFinite(bounds.X) ? bounds.X : 0d;
        var y = IsFinite(bounds.Y) ? bounds.Y : 0d;
        var width = IsFinite(bounds.Width) && bounds.Width > 0
            ? Math.Max(MinWidth, bounds.Width)
            : MinWidth;
        var height = IsFinite(bounds.Height) && bounds.Height > 0
            ? Math.Max(MinHeight, bounds.Height)
            : MinHeight;
        return new Rect(x, y, width, height);
    }

    public static PixelPoint ToPixelPoint(Point point)
    {
        return new PixelPoint(
            (int)Math.Round(point.X),
            (int)Math.Round(point.Y));
    }

    public static PixelPoint ClampPosition(
        TopLevel? owner,
        PixelPoint desiredPosition,
        double width,
        double height)
    {
        if (owner is null)
        {
            return desiredPosition;
        }

        var screens = owner.Screens;
        if (screens is null)
        {
            return desiredPosition;
        }

        var windowWidth = Math.Max(1, (int)Math.Ceiling(width));
        var windowHeight = Math.Max(1, (int)Math.Ceiling(height));
        var desiredBounds = new PixelRect(
            desiredPosition.X,
            desiredPosition.Y,
            windowWidth,
            windowHeight);
        var screen = screens.ScreenFromBounds(desiredBounds)
            ?? screens.ScreenFromPoint(desiredPosition)
            ?? screens.ScreenFromTopLevel(owner)
            ?? screens.Primary;
        if (screen is null)
        {
            return desiredPosition;
        }

        var workingArea = screen.WorkingArea;
        var maxX = Math.Max(workingArea.X, workingArea.Right - windowWidth);
        var maxY = Math.Max(workingArea.Y, workingArea.Bottom - windowHeight);
        return new PixelPoint(
            Math.Clamp(desiredPosition.X, workingArea.X, maxX),
            Math.Clamp(desiredPosition.Y, workingArea.Y, maxY));
    }

    private static bool IsFinite(double value)
    {
        return !double.IsNaN(value) && !double.IsInfinity(value);
    }
}
