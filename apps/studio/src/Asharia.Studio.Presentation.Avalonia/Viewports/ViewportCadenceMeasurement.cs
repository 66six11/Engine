using System;
using System.Diagnostics;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

/// <summary>Bounded smoke-only samples; every statistic uses the same explicit window.</summary>
internal sealed class ViewportCadenceMeasurement
{
    private const int Capacity = 4096;
    private readonly object gate_ = new();
    private readonly long[] timestamps_ = new long[Capacity];
    private long startedAt_;
    private int count_;
    private bool measuring_;
    private bool overflowed_;

    public void Begin(long timestamp)
    {
        lock (gate_)
        {
            startedAt_ = timestamp;
            count_ = 0;
            overflowed_ = false;
            measuring_ = true;
        }
    }

    public void Record(long timestamp)
    {
        lock (gate_)
        {
            if (!measuring_ || timestamp < startedAt_) return;
            if (count_ == Capacity)
            {
                overflowed_ = true;
                return;
            }
            timestamps_[count_++] = timestamp;
        }
    }

    public ViewportCadenceMeasurementResult End(long timestamp)
    {
        lock (gate_)
        {
            if (!measuring_ || timestamp <= startedAt_)
                throw new InvalidOperationException("Cadence measurement requires a positive active window.");
            measuring_ = false;
            if (overflowed_)
                throw new InvalidOperationException("Cadence measurement exceeded its bounded sample capacity.");
            var count = 0;
            while (count < count_ && timestamps_[count] <= timestamp) count++;
            var intervals = new long[Math.Max(0, count - 1)];
            for (var i = 0; i < intervals.Length; i++)
            {
                intervals[i] = timestamps_[i + 1] - timestamps_[i];
                if (intervals[i] < 0)
                    throw new InvalidOperationException("Cadence timestamps must be monotonic.");
            }
            Array.Sort(intervals);
            var p95 = intervals.Length == 0 ? 0 : intervals[(int)Math.Ceiling(intervals.Length * .95) - 1];
            var maximum = intervals.Length == 0 ? 0 : intervals[^1];
            return new(count, Stopwatch.GetElapsedTime(startedAt_, timestamp),
                Stopwatch.GetElapsedTime(0, p95), Stopwatch.GetElapsedTime(0, maximum));
        }
    }
}

internal readonly record struct ViewportCadenceMeasurementResult(
    int Frames, TimeSpan Elapsed, TimeSpan P95FrameInterval, TimeSpan MaximumFrameInterval)
{
    public double FramesPerSecond => Frames / Elapsed.TotalSeconds;
}
