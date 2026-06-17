using Avalonia;
using Editor.Shell.Views.Windowing;
using Xunit;

namespace Editor.Tests.Shell.Views.Windowing;

public sealed class EditorDockFloatingWindowPlacementTests
{
    [Fact]
    public void NormalizeBounds_recovers_invalid_values_and_enforces_minimum_size()
    {
        var bounds = EditorDockFloatingWindowPlacement.NormalizeBounds(
            new Rect(double.NaN, double.PositiveInfinity, -1, 4));

        Assert.Equal(0, bounds.X);
        Assert.Equal(0, bounds.Y);
        Assert.Equal(EditorDockFloatingWindowPlacement.MinWidth, bounds.Width);
        Assert.Equal(EditorDockFloatingWindowPlacement.MinHeight, bounds.Height);
    }

    [Fact]
    public void ClampPosition_uses_scaled_pixel_size_for_logical_window_bounds()
    {
        var clamped = EditorDockFloatingWindowPlacement.ClampPosition(
            new PixelRect(0, 0, 1000, 800),
            new PixelPoint(800, 700),
            width: 200,
            height: 100,
            renderScaling: 2);

        Assert.Equal(new PixelPoint(600, 600), clamped);
    }
}
