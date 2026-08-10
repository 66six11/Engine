using System;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportPhysicalExtentPolicyTests
{
    [Theory]
    [InlineData(1.00, 640, 480)]
    [InlineData(1.25, 800, 600)]
    [InlineData(1.50, 960, 720)]
    [InlineData(2.00, 1280, 960)]
    public void Standard_DPI_matrix_produces_exact_physical_extents(
        double scaling,
        uint expectedWidth,
        uint expectedHeight)
    {
        Assert.True(ViewportPhysicalExtentPolicy.TryCalculate(
            logicalWidth: 640,
            logicalHeight: 480,
            scaling,
            out var extent));

        Assert.Equal(expectedWidth, extent.Width);
        Assert.Equal(expectedHeight, extent.Height);
    }

    [Theory]
    [InlineData(1.00)]
    [InlineData(1.25)]
    [InlineData(1.50)]
    [InlineData(2.00)]
    public void One_pixel_rounding_boundary_is_outward_at_each_supported_scaling(
        double scaling)
    {
        const double pixelBoundary = 100;
        var belowBoundary = (pixelBoundary - 0.25) / scaling;
        var aboveBoundary = (pixelBoundary + 0.25) / scaling;

        Assert.True(ViewportPhysicalExtentPolicy.TryCalculate(
            belowBoundary,
            belowBoundary,
            scaling,
            out var below));
        Assert.True(ViewportPhysicalExtentPolicy.TryCalculate(
            aboveBoundary,
            aboveBoundary,
            scaling,
            out var above));

        Assert.Equal((uint)pixelBoundary, below.Width);
        Assert.Equal((uint)pixelBoundary, below.Height);
        Assert.Equal((uint)pixelBoundary + 1, above.Width);
        Assert.Equal((uint)pixelBoundary + 1, above.Height);
    }

    [Theory]
    [InlineData(1.00)]
    [InlineData(1.25)]
    [InlineData(1.50)]
    [InlineData(2.00)]
    public void Subpixel_renderable_bounds_still_allocate_one_exact_pixel(double scaling)
    {
        var logicalSize = 0.5 / scaling;

        Assert.True(ViewportPhysicalExtentPolicy.TryCalculate(
            logicalSize,
            logicalSize,
            scaling,
            out var extent));
        Assert.Equal(1u, extent.Width);
        Assert.Equal(1u, extent.Height);
    }

    [Theory]
    [InlineData(0, 1, 1)]
    [InlineData(1, 0, 1)]
    [InlineData(1, 1, 0)]
    [InlineData(double.NaN, 1, 1)]
    [InlineData(1, double.PositiveInfinity, 1)]
    [InlineData(1, 1, double.NaN)]
    public void Non_renderable_geometry_is_rejected(
        double width,
        double height,
        double scaling)
    {
        Assert.False(ViewportPhysicalExtentPolicy.TryCalculate(
            width,
            height,
            scaling,
            out _));
    }
}
