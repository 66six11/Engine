using System;

namespace Asharia.Editor.Viewports;

public sealed record ViewportSchedulerContext
{
    public ViewportSchedulerContext(
        DateTimeOffset nowUtc,
        int maxViewportRendersThisTick = 2)
    {
        if (maxViewportRendersThisTick <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxViewportRendersThisTick),
                maxViewportRendersThisTick,
                "Maximum viewport renders per tick must be greater than zero.");
        }

        NowUtc = nowUtc;
        MaxViewportRendersThisTick = maxViewportRendersThisTick;
    }

    public DateTimeOffset NowUtc { get; }

    public int MaxViewportRendersThisTick { get; }
}
