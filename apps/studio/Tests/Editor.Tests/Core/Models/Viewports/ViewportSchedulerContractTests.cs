using System;
using Editor.Core.Models.Viewports;
using Xunit;

namespace Editor.Tests.Core.Models.Viewports;

public sealed class ViewportSchedulerContractTests
{
    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    public void Scheduler_context_rejects_non_positive_render_limit(
        int maxViewportRendersThisTick)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerContext(
                DateTimeOffset.UnixEpoch,
                maxViewportRendersThisTick));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    public void Scheduler_options_reject_invalid_frame_rates(double framesPerSecond)
    {
        AssertInvalidFrameRate(framesPerSecond);
    }

    [Fact]
    public void Scheduler_options_reject_unrepresentable_frame_rates()
    {
        AssertInvalidFrameRate(double.Epsilon);
    }

    [Fact]
    public void Scheduler_options_expose_ceiling_based_intervals()
    {
        var options = new ViewportSchedulerOptions(
            interactiveBurstFramesPerSecond: 60,
            sceneIdleFramesPerSecond: 5,
            previewFramesPerSecond: 15,
            runtimeFramesPerSecond: 30);

        Assert.Equal(TimeSpan.FromTicks(166667), options.InteractiveBurstInterval);
        Assert.Equal(TimeSpan.FromMilliseconds(200), options.SceneIdleInterval);
        Assert.Equal(TimeSpan.FromTicks(666667), options.PreviewInterval);
        Assert.Equal(TimeSpan.FromTicks(333334), options.RuntimeInterval);
    }

    [Fact]
    public void Render_request_rejects_empty_reason()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportRenderRequest(
                new ViewportId("scene"),
                ViewportKind.Scene,
                CreateExtent(),
                CreateClock(),
                ViewportUpdatePolicy.DirtyOnly,
                ViewportRenderReason.None,
                priority: 10,
                DateTimeOffset.UnixEpoch));
    }

    [Fact]
    public void Render_request_rejects_negative_priority()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportRenderRequest(
                new ViewportId("scene"),
                ViewportKind.Scene,
                CreateExtent(),
                CreateClock(),
                ViewportUpdatePolicy.DirtyOnly,
                ViewportRenderReason.InitialFrameMissing,
                priority: -1,
                DateTimeOffset.UnixEpoch));
    }

    [Fact]
    public void Render_result_rejects_default_id()
    {
        Assert.Throws<ArgumentException>(
            () => new ViewportRenderResult(
                default,
                ViewportKind.Scene,
                CreateExtent(),
                ViewportRenderReason.InitialFrameMissing,
                wasRendered: true,
                "Rendered",
                DateTimeOffset.UnixEpoch,
                cpuMilliseconds: 0.25));
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    public void Render_result_rejects_invalid_cpu_duration(double cpuMilliseconds)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportRenderResult(
                new ViewportId("scene"),
                ViewportKind.Scene,
                CreateExtent(),
                ViewportRenderReason.InitialFrameMissing,
                wasRendered: true,
                "Rendered",
                DateTimeOffset.UnixEpoch,
                cpuMilliseconds));
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

    private static void AssertInvalidFrameRate(double framesPerSecond)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerOptions(
                interactiveBurstFramesPerSecond: framesPerSecond));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerOptions(
                sceneIdleFramesPerSecond: framesPerSecond));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerOptions(
                previewFramesPerSecond: framesPerSecond));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerOptions(
                runtimeFramesPerSecond: framesPerSecond));
    }
}
