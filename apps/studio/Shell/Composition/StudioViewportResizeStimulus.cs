using System;
using System.Diagnostics;
using System.Threading.Tasks;

namespace Editor.Shell.Composition;

internal static class StudioViewportResizeStimulus
{
    public static double[] Build(
        string pattern,
        int inputCount,
        double originWidth,
        double renderScaling = 1)
    {
        if (!double.IsFinite(originWidth) || originWidth <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(originWidth));
        }
        if (!double.IsFinite(renderScaling) || renderScaling <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(renderScaling));
        }
        var result = new double[inputCount];
        var random = new Random(0x5A17);
        var shrinkTravel = Math.Min(360, Math.Max(1, originWidth - 64));
        for (var index = 0; index < result.Length; index++)
        {
            var progress = result.Length == 1 ? 1 : (double)index / (result.Length - 1);
            var directionalProgress = (index + 1d) / result.Length;
            result[index] = pattern switch
            {
                "grow" => originWidth + directionalProgress * 360,
                "shrink" => originWidth - directionalProgress * shrinkTravel,
                "aba" => originWidth + 360 * (1 - Math.Abs(progress * 2 - 1)),
                "jitter" => originWidth + random.Next(-220, 221),
                "pixel" => originWidth + (index + 1) / renderScaling,
                _ => 460 + (index % 18) * 32,
            };
        }
        if (pattern == "aba")
        {
            result[^1] = originWidth;
        }
        return result;
    }

    public static async Task WaitUntilAsync(long startedAt, double seconds)
    {
        var target = startedAt + checked((long)Math.Round(seconds * Stopwatch.Frequency));
        while (true)
        {
            var remaining = Stopwatch.GetElapsedTime(Stopwatch.GetTimestamp(), target);
            if (remaining <= TimeSpan.Zero)
            {
                return;
            }
            if (remaining > TimeSpan.FromMilliseconds(2))
            {
                await Task.Delay(remaining - TimeSpan.FromMilliseconds(1));
            }
            else
            {
                await Task.Yield();
            }
        }
    }
}
