using System;
using Asharia.Editor.Viewports;
using Xunit;

namespace Asharia.Editor.Tests.Viewports;

public sealed class ViewportModelTests
{
    [Theory]
    [InlineData("")]
    [InlineData(" ")]
    [InlineData("\t")]
    public void Viewport_id_rejects_blank_values(string value)
    {
        Assert.Throws<ArgumentException>(() => new ViewportId(value));
    }

    [Fact]
    public void Viewport_id_trims_stable_value()
    {
        var id = new ViewportId(" scene ");

        Assert.Equal("scene", id.Value);
        Assert.Equal("scene", id.ToString());
    }

    [Theory]
    [InlineData(0, 480, 1)]
    [InlineData(640, 0, 1)]
    [InlineData(-1, 480, 1)]
    [InlineData(640, -1, 1)]
    [InlineData(640, 480, 0)]
    [InlineData(640, 480, -1)]
    [InlineData(640, 480, double.NaN)]
    [InlineData(640, 480, double.PositiveInfinity)]
    public void Viewport_extent_rejects_invalid_values(
        int widthPixels,
        int heightPixels,
        double renderScale)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportExtent(widthPixels, heightPixels, renderScale));
    }

    [Theory]
    [InlineData((ViewportKind)42)]
    public void Viewport_state_rejects_unknown_kind(ViewportKind kind)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportStateSnapshot(
                new ViewportId("scene"),
                "Scene",
                kind,
                CreateExtent(),
                ViewportUpdatePolicy.DirtyOnly,
                CreateClock(),
                isVisible: true,
                isFocused: false,
                isDirty: false,
                lastRenderedAtUtc: null,
                interactiveBurstRemaining: TimeSpan.Zero,
                pendingReasons: ViewportRenderReason.None));
    }

    [Fact]
    public void Viewport_state_rejects_default_id()
    {
        Assert.Throws<ArgumentException>(
            () => new ViewportStateSnapshot(
                default,
                "Scene",
                ViewportKind.Scene,
                CreateExtent(),
                ViewportUpdatePolicy.DirtyOnly,
                CreateClock(),
                isVisible: true,
                isFocused: false,
                isDirty: false,
                lastRenderedAtUtc: null,
                interactiveBurstRemaining: TimeSpan.Zero,
                pendingReasons: ViewportRenderReason.None));
    }

    [Theory]
    [InlineData(-1, 0, 0, 1)]
    [InlineData(0, -1, 0, 1)]
    [InlineData(0, 0, 0, 0)]
    [InlineData(double.NaN, 0, 0, 1)]
    [InlineData(0, double.NaN, 0, 1)]
    [InlineData(0, 0, 0, double.PositiveInfinity)]
    public void Viewport_clock_rejects_invalid_values(
        double timeSeconds,
        double deltaSeconds,
        ulong frameIndex,
        double playbackSpeed)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportClockSnapshot(
                ViewportClockMode.EditorPreviewTime,
                timeSeconds,
                deltaSeconds,
                frameIndex,
                playbackSpeed));
    }

    [Fact]
    public void Viewport_clock_advance_uses_caller_supplied_time_step()
    {
        var clock = new ViewportClockSnapshot(
            ViewportClockMode.EditorPreviewTime,
            timeSeconds: 2,
            deltaSeconds: 0,
            frameIndex: 7,
            playbackSpeed: 0.5);

        var advanced = clock.Advance(TimeSpan.FromSeconds(4));

        Assert.Equal(4, advanced.TimeSeconds);
        Assert.Equal(2, advanced.DeltaSeconds);
        Assert.Equal<ulong>(8, advanced.FrameIndex);
        Assert.Equal(0.5, advanced.PlaybackSpeed);
    }

    [Theory]
    [InlineData(ViewportClockMode.FrozenTime)]
    [InlineData(ViewportClockMode.ManualStepTime)]
    [InlineData(ViewportClockMode.CapturedFrameTime)]
    public void Viewport_clock_advance_keeps_manual_or_frozen_time_stable(
        ViewportClockMode mode)
    {
        var clock = new ViewportClockSnapshot(
            mode,
            timeSeconds: 2,
            deltaSeconds: 0.25,
            frameIndex: 7,
            playbackSpeed: 1);

        var advanced = clock.Advance(TimeSpan.FromSeconds(4));

        Assert.Equal(2, advanced.TimeSeconds);
        Assert.Equal(0, advanced.DeltaSeconds);
        Assert.Equal<ulong>(7, advanced.FrameIndex);
    }

    private static ViewportExtent CreateExtent()
    {
        return new ViewportExtent(640, 480, renderScale: 1);
    }

    private static ViewportClockSnapshot CreateClock()
    {
        return new ViewportClockSnapshot(
            ViewportClockMode.EditorPreviewTime,
            timeSeconds: 0,
            deltaSeconds: 0,
            frameIndex: 0,
            playbackSpeed: 1);
    }
}
