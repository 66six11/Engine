using System;
using Asharia.Studio.Application.Viewports;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal static class ViewportPhysicalExtentPolicy
{
    public static bool TryCalculate(
        double logicalWidth,
        double logicalHeight,
        double renderScaling,
        out ViewportExtent extent)
    {
        extent = default;
        if (!double.IsFinite(logicalWidth) || logicalWidth <= 0 ||
            !double.IsFinite(logicalHeight) || logicalHeight <= 0 ||
            !double.IsFinite(renderScaling) || renderScaling <= 0)
        {
            return false;
        }

        var width = Math.Clamp(
            Math.Ceiling(logicalWidth * renderScaling),
            1,
            uint.MaxValue);
        var height = Math.Clamp(
            Math.Ceiling(logicalHeight * renderScaling),
            1,
            uint.MaxValue);
        extent = new ViewportExtent(
            checked((uint)width),
            checked((uint)height));
        return true;
    }
}
