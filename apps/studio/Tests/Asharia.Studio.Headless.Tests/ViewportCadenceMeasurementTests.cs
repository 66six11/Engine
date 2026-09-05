using System;
using System.Diagnostics;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportCadenceMeasurementTests
{
    [Fact]
    public void Window_excludes_warmup_and_straddling_intervals()
    {
        var measurement = new ViewportCadenceMeasurement();
        var second = Stopwatch.Frequency;
        measurement.Record(0);
        measurement.Begin(second);
        measurement.Record(second - 1);
        measurement.Record(second + second / 10);
        measurement.Record(second + 2 * second / 10);
        measurement.Record(3 * second);
        var result = measurement.End(2 * second);
        Assert.Equal(2, result.Frames);
        Assert.Equal(2, result.FramesPerSecond);
        Assert.Equal(TimeSpan.FromSeconds(.1), result.P95FrameInterval);
        Assert.Equal(result.P95FrameInterval, result.MaximumFrameInterval);
    }

    [Fact]
    public void Percentile_uses_nearest_rank_from_the_measured_intervals()
    {
        var measurement = new ViewportCadenceMeasurement();
        measurement.Begin(0);
        long timestamp = 0;
        measurement.Record(timestamp);
        for (var i = 1; i <= 20; i++) measurement.Record(timestamp += i * Stopwatch.Frequency);
        var result = measurement.End(timestamp);
        Assert.Equal(21, result.Frames);
        Assert.Equal(TimeSpan.FromSeconds(19), result.P95FrameInterval);
        Assert.Equal(TimeSpan.FromSeconds(20), result.MaximumFrameInterval);
    }

    [Fact]
    public void Overflow_is_reported_instead_of_silently_truncating_the_window()
    {
        var measurement = new ViewportCadenceMeasurement();
        measurement.Begin(0);
        for (var i = 0; i < 4097; i++) measurement.Record(i);
        Assert.Throws<InvalidOperationException>(() => measurement.End(5000));
    }
}
