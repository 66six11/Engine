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
}
