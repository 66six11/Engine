using System;
using System.Linq;
using Asharia.Editor.Viewports;
using Asharia.Studio.Application.Viewports;
using Xunit;

namespace Asharia.Studio.Application.Tests.Viewports;

public sealed class ViewportSchedulerTests
{
    [Fact]
    public void Hidden_viewport_is_skipped()
    {
        var scheduler = new ViewportScheduler();
        var viewport = CreateViewport(
            isVisible: false,
            isDirty: true,
            pendingReasons: ViewportRenderReason.Resized);

        var requests = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(DateTimeOffset.UnixEpoch));

        Assert.Empty(requests);
    }

    [Fact]
    public void Dirty_only_viewport_renders_only_when_dirty_or_reasoned()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var clean = CreateViewport(
            id: "clean",
            updatePolicy: ViewportUpdatePolicy.DirtyOnly,
            lastRenderedAtUtc: now);
        var dirty = CreateViewport(
            id: "dirty",
            updatePolicy: ViewportUpdatePolicy.DirtyOnly,
            isDirty: true,
            lastRenderedAtUtc: now);
        var reasoned = CreateViewport(
            id: "reasoned",
            updatePolicy: ViewportUpdatePolicy.DirtyOnly,
            lastRenderedAtUtc: now,
            pendingReasons: ViewportRenderReason.CameraChanged);

        var requests = scheduler.BuildRenderPlan(
            [clean, dirty, reasoned],
            new ViewportSchedulerContext(now.AddMilliseconds(16)));

        Assert.Equal(["dirty", "reasoned"], requests.Select(request => request.Id.Value));
        Assert.Contains(
            requests,
            request => request.Id.Value == "dirty"
                && request.Reason.HasFlag(ViewportRenderReason.AssetChanged));
        Assert.Contains(
            requests,
            request => request.Id.Value == "reasoned"
                && request.Reason.HasFlag(ViewportRenderReason.CameraChanged));
    }

    [Fact]
    public void Interactive_viewport_bursts_then_uses_idle_interval()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var active = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.InteractiveBurst,
            isFocused: true,
            lastRenderedAtUtc: now,
            interactiveBurstRemaining: TimeSpan.FromSeconds(1));
        var idle = CreateViewport(
            id: "idle",
            updatePolicy: ViewportUpdatePolicy.InteractiveBurst,
            lastRenderedAtUtc: now,
            interactiveBurstRemaining: TimeSpan.Zero);

        var activeRequests = scheduler.BuildRenderPlan(
            [active],
            new ViewportSchedulerContext(now.AddMilliseconds(17)));
        var idleTooSoon = scheduler.BuildRenderPlan(
            [idle],
            new ViewportSchedulerContext(now.AddMilliseconds(100)));
        var idleAtInterval = scheduler.BuildRenderPlan(
            [idle],
            new ViewportSchedulerContext(now.AddMilliseconds(200)));

        var activeRequest = Assert.Single(activeRequests);
        Assert.True(activeRequest.Reason.HasFlag(ViewportRenderReason.InputActive));
        Assert.Empty(idleTooSoon);

        var idleRequest = Assert.Single(idleAtInterval);
        Assert.True(idleRequest.Reason.HasFlag(ViewportRenderReason.VisibleExposed));
    }

    [Fact]
    public void Interactive_burst_respects_configured_interval()
    {
        var scheduler = new ViewportScheduler(new ViewportSchedulerOptions(
            interactiveBurstFramesPerSecond: 10,
            sceneIdleFramesPerSecond: 5,
            previewFramesPerSecond: 15,
            runtimeFramesPerSecond: 60));
        var now = DateTimeOffset.UnixEpoch;
        var viewport = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.InteractiveBurst,
            isFocused: true,
            lastRenderedAtUtc: now,
            interactiveBurstRemaining: TimeSpan.FromSeconds(1));

        var tooSoon = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(now.AddMilliseconds(99)));
        var atInterval = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(now.AddMilliseconds(100)));

        Assert.Empty(tooSoon);

        var request = Assert.Single(atInterval);
        Assert.True(request.Reason.HasFlag(ViewportRenderReason.InputActive));
    }

    [Fact]
    public void Time_playback_request_keeps_caller_supplied_clock()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var clock = new ViewportClockSnapshot(
            ViewportClockMode.EditorPreviewTime,
            timeSeconds: 4.5,
            deltaSeconds: 0.5,
            frameIndex: 17,
            playbackSpeed: 1);
        var viewport = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.TimePlayback,
            clock: clock,
            lastRenderedAtUtc: now);

        var requests = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(now.AddMilliseconds(100)));

        var request = Assert.Single(requests);
        Assert.Same(clock, request.Clock);
        Assert.True(request.Reason.HasFlag(ViewportRenderReason.TimeAdvanced));
    }

    [Fact]
    public void Time_playback_with_future_last_rendered_time_still_renders()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var viewport = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.TimePlayback,
            lastRenderedAtUtc: now.AddSeconds(1));

        var requests = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(now));

        var request = Assert.Single(requests);
        Assert.True(request.Reason.HasFlag(ViewportRenderReason.TimeAdvanced));
    }

    [Fact]
    public void Runtime_play_renders_at_runtime_interval()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var viewport = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.RuntimePlay,
            isFocused: true,
            lastRenderedAtUtc: now);

        var requests = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(now.AddMilliseconds(17)));

        var request = Assert.Single(requests);
        Assert.True(request.Reason.HasFlag(ViewportRenderReason.RuntimePlaying));
    }

    [Fact]
    public void Performance_preview_renders_at_runtime_interval()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var viewport = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.PerformancePreview,
            isFocused: true,
            lastRenderedAtUtc: now);

        var requests = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(now.AddMilliseconds(17)));

        var request = Assert.Single(requests);
        Assert.True(request.Reason.HasFlag(ViewportRenderReason.RuntimePlaying));
    }

    [Fact]
    public void Frame_debug_viewport_renders_only_initial_step_capture_or_dirty_reasons()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var clean = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.FrameDebug,
            lastRenderedAtUtc: now);
        var stepped = CreateViewport(
            id: "stepped",
            updatePolicy: ViewportUpdatePolicy.FrameDebug,
            lastRenderedAtUtc: now,
            pendingReasons: ViewportRenderReason.FrameDebugStep);

        var cleanRequests = scheduler.BuildRenderPlan(
            [clean],
            new ViewportSchedulerContext(now.AddSeconds(1)));
        var steppedRequests = scheduler.BuildRenderPlan(
            [stepped],
            new ViewportSchedulerContext(now.AddSeconds(1)));

        Assert.Empty(cleanRequests);
        var steppedRequest = Assert.Single(steppedRequests);
        Assert.Equal(ViewportRenderReason.FrameDebugStep, steppedRequest.Reason);
    }

    [Fact]
    public void Scheduler_limits_requests_and_orders_hot_viewports_first()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var low = CreateViewport(
            id: "material",
            kind: ViewportKind.MaterialPreview,
            updatePolicy: ViewportUpdatePolicy.DirtyOnly,
            isDirty: true,
            lastRenderedAtUtc: now);
        var focused = CreateViewport(
            id: "scene",
            updatePolicy: ViewportUpdatePolicy.InteractiveBurst,
            isFocused: true,
            lastRenderedAtUtc: now,
            interactiveBurstRemaining: TimeSpan.FromSeconds(1));
        var capture = CreateViewport(
            id: "frame",
            kind: ViewportKind.FrameDebug,
            updatePolicy: ViewportUpdatePolicy.FrameDebug,
            lastRenderedAtUtc: now,
            pendingReasons: ViewportRenderReason.CaptureRequested);

        var requests = scheduler.BuildRenderPlan(
            [low, focused, capture],
            new ViewportSchedulerContext(now.AddMilliseconds(17), maxViewportRendersThisTick: 2));

        Assert.Equal(["frame", "scene"], requests.Select(request => request.Id.Value));
    }

    private static ViewportStateSnapshot CreateViewport(
        string id = "scene",
        ViewportKind kind = ViewportKind.Scene,
        ViewportUpdatePolicy updatePolicy = ViewportUpdatePolicy.DirtyOnly,
        bool isVisible = true,
        bool isFocused = false,
        bool isDirty = false,
        DateTimeOffset? lastRenderedAtUtc = null,
        TimeSpan? interactiveBurstRemaining = null,
        ViewportRenderReason pendingReasons = ViewportRenderReason.None,
        ViewportClockSnapshot? clock = null)
    {
        return new ViewportStateSnapshot(
            new ViewportId(id),
            id,
            kind,
            updatePolicy == ViewportUpdatePolicy.FrameDebug
                ? new ViewportExtent(320, 180, renderScale: 1)
                : new ViewportExtent(640, 480, renderScale: 1),
            updatePolicy,
            clock ?? new ViewportClockSnapshot(
                ViewportClockMode.EditorPreviewTime,
                timeSeconds: 0,
                deltaSeconds: 0,
                frameIndex: 0,
                playbackSpeed: 1),
            isVisible,
            isFocused,
            isDirty,
            lastRenderedAtUtc,
            interactiveBurstRemaining ?? TimeSpan.Zero,
            pendingReasons);
    }
}
