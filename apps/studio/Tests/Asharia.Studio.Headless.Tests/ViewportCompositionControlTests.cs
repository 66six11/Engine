using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportCompositionControlTests
{
    [Fact]
    public void Presented_frame_sequences_are_strictly_monotonic()
    {
        var epoch = 7UL;
        var sessionId = ViewportSessionId.Create();
        var targetId = Guid.NewGuid();
        var current = Snapshot(sessionId, targetId, revision: 4);
        var frameA = Frame(sessionId, targetId, revision: 4, sequence: 1);
        var frameB = Frame(sessionId, targetId, revision: 4, sequence: 2);
        var frameC = Frame(sessionId, targetId, revision: 4, sequence: 3);
        var state = new ViewportFramePresentationState();
        state.Reset(epoch);

        Assert.True(state.TryMarkPresented(epoch, frameA, current));
        Assert.True(state.TryMarkPresented(epoch, frameB, current));
        Assert.True(state.TryMarkPresented(epoch, frameC, current));
        Assert.Equal(frameC.Sequence, state.LastPresentedSequence);
        Assert.False(state.CanPresent(epoch, frameA, current));
        Assert.False(state.TryMarkPresented(epoch, frameB, current));
    }

    [Fact]
    public void Resize_presents_only_exact_current_panel_extent()
    {
        var exact = new ViewportRenderSize(
            new ViewportExtent(641, 480),
            new ViewportExtent(641, 480));
        var advanced = new ViewportRenderSize(
            new ViewportExtent(642, 480),
            new ViewportExtent(642, 480));
        var padded = new ViewportRenderSize(
            new ViewportExtent(641, 480),
            new ViewportExtent(704, 512));

        Assert.True(ViewportResizePresentationPolicy.CanPresentCompletedFrame(
            exact,
            exact));
        Assert.False(ViewportResizePresentationPolicy.CanPresentCompletedFrame(
            exact,
            advanced));
        Assert.False(ViewportResizePresentationPolicy.CanPresentCompletedFrame(
            padded,
            exact));
        Assert.False(ViewportResizePresentationPolicy.CanPresentCompletedFrame(
            default,
            exact));
    }

    [Fact]
    public void Returning_to_an_old_extent_does_not_reexpose_its_stale_surface_generation()
    {
        var sizeA = new ViewportExtent(640, 480);
        var sizeB = new ViewportExtent(800, 480);
        var state = new ViewportGeometryGenerationState();

        state.Synchronize(sizeA);
        var firstSizeAGeneration = state.CurrentGeneration;
        state.MarkSurfaceUpdate(sizeA, firstSizeAGeneration);
        Assert.True(state.HasExactSurface);

        state.Synchronize(sizeB);
        Assert.False(state.HasExactSurface);
        state.Synchronize(sizeA);
        Assert.NotEqual(firstSizeAGeneration, state.CurrentGeneration);
        Assert.False(state.HasExactSurface);
        Assert.Throws<InvalidOperationException>(
            () => state.MarkSurfaceUpdate(sizeA, firstSizeAGeneration));

        state.MarkSurfaceUpdate(sizeA, state.CurrentGeneration);
        Assert.True(state.HasExactSurface);
    }

    [Fact]
    public void Consumer_access_tracker_distinguishes_all_release_boundaries()
    {
        var tracker = new CompositionConsumerAccessTracker();

        Assert.Equal(
            CompositionConsumerAccessState.NotSubmittedToConsumer,
            tracker.State);

        tracker.MarkSubmissionStarted();
        Assert.Equal(
            CompositionConsumerAccessState.SubmissionStarted,
            tracker.State);

        tracker.MarkConsumerAccessed();
        Assert.Equal(
            CompositionConsumerAccessState.ConsumerAccessed,
            tracker.State);
    }

    [Fact]
    public void Consumer_access_tracker_rejects_missing_or_duplicate_transitions()
    {
        var missingSubmission = new CompositionConsumerAccessTracker();
        Assert.Throws<InvalidOperationException>(missingSubmission.MarkConsumerAccessed);

        var duplicateSubmission = new CompositionConsumerAccessTracker();
        duplicateSubmission.MarkSubmissionStarted();
        Assert.Throws<InvalidOperationException>(duplicateSubmission.MarkSubmissionStarted);
        duplicateSubmission.MarkConsumerAccessed();
        Assert.Throws<InvalidOperationException>(duplicateSubmission.MarkConsumerAccessed);
    }

    [Fact]
    public async Task New_stream_pump_starts_while_old_stream_waits_for_its_own_pump()
    {
        var oldPumpCompletion = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var oldFence = new ViewportStreamWorkFence();
        var newFence = new ViewportStreamWorkFence();
        var oldResourcesReleased = false;

        Assert.True(oldFence.TryStartPump(
            () => oldPumpCompletion.Task,
            out var oldPump));
        var oldRetirement = oldFence.BeginRetirement(
            requestClose: () => { },
            releaseResources: () =>
            {
                oldResourcesReleased = true;
                return Task.CompletedTask;
            });

        Assert.True(newFence.TryStartPump(() => Task.CompletedTask, out var newPump));
        await newPump;
        Assert.Same(oldFence.PumpTask, oldPump);
        Assert.False(oldResourcesReleased);
        Assert.False(oldRetirement.IsCompleted);

        oldPumpCompletion.SetResult(true);
        await oldRetirement;
        Assert.True(oldResourcesReleased);
    }

    [Fact]
    public async Task Pump_start_closes_synchronous_reentry_before_invoking_factory()
    {
        var fence = new ViewportStreamWorkFence();
        var reentrantStartSucceeded = true;

        Assert.True(fence.TryStartPump(
            () =>
            {
                reentrantStartSucceeded = fence.TryStartPump(
                    () => Task.CompletedTask,
                    out _);
                return Task.CompletedTask;
            },
            out var pump));

        await pump;
        Assert.False(reentrantStartSucceeded);
    }

    [Fact]
    public async Task Retirement_releases_resources_after_owned_pump_and_presentations()
    {
        var pumpCompletion = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var presentationCompletion = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var fence = new ViewportStreamWorkFence();
        var releaseCount = 0;

        Assert.True(fence.TryStartPump(() => pumpCompletion.Task, out _));
        fence.TrackPresentation(presentationCompletion.Task);
        var retirement = fence.BeginRetirement(
            requestClose: () => { },
            releaseResources: () =>
            {
                releaseCount++;
                return Task.CompletedTask;
            });

        Assert.False(fence.TryStartPump(() => Task.CompletedTask, out _));
        pumpCompletion.SetResult(true);
        await Task.Yield();
        Assert.Equal(0, releaseCount);

        presentationCompletion.SetResult(true);
        await retirement;
        Assert.Equal(1, releaseCount);
    }

    [Fact]
    public async Task Close_failure_quarantines_resources_from_release_and_does_not_block_new_stream()
    {
        var oldFence = new ViewportStreamWorkFence();
        var newFence = new ViewportStreamWorkFence();
        var releaseCount = 0;
        var retirement = oldFence.BeginRetirement(
            requestClose: () => throw new InvalidOperationException("close failed"),
            releaseResources: () =>
            {
                releaseCount++;
                return Task.CompletedTask;
            });

        var failure = await Assert.ThrowsAsync<InvalidOperationException>(() => retirement);
        Assert.Equal("close failed", failure.Message);
        Assert.Equal(0, releaseCount);
        Assert.True(oldFence.IsRetiring);
        Assert.False(oldFence.TryStartPump(() => Task.CompletedTask, out _));
        Assert.True(newFence.TryStartPump(() => Task.CompletedTask, out var newPump));
        await newPump;
    }

    [Fact]
    public void Presentation_epoch_and_scene_revision_reject_stale_resize_frames()
    {
        var epoch = 11UL;
        var sessionId = ViewportSessionId.Create();
        var targetId = Guid.NewGuid();
        var oldFrame = Frame(sessionId, targetId, revision: 8, sequence: 1);
        var state = new ViewportFramePresentationState();
        state.Reset(epoch);

        Assert.False(state.CanPresent(
            epoch,
            oldFrame,
            Snapshot(sessionId, targetId, revision: 9)));

        state.Reset(epoch + 1);
        Assert.False(state.CanPresent(
            epoch,
            oldFrame,
            Snapshot(sessionId, targetId, revision: 8)));
    }

    [Fact]
    public void Presentation_cadence_gate_accepts_above_60_and_rejects_below_60_fps()
    {
        var fast = new ViewportPresentationCadenceTracker();
        RecordCadence(fast, framesPerSecond: 120, frameCount: 240);
        var fastMetrics = fast.Capture();

        var slow = new ViewportPresentationCadenceTracker();
        RecordCadence(slow, framesPerSecond: 30, frameCount: 120);
        var slowMetrics = slow.Capture();

        Assert.True(fastMetrics.MeetsMinimumFramesPerSecond(60));
        Assert.InRange(fastMetrics.FramesPerSecond, 119.9, 120.1);
        Assert.False(slowMetrics.MeetsMinimumFramesPerSecond(60));
        Assert.InRange(slowMetrics.FramesPerSecond, 29.9, 30.1);
    }

    [Fact]
    public void Presentation_cadence_reports_p95_maximum_and_wrapped_window()
    {
        var tracker = new ViewportPresentationCadenceTracker();
        var timestamp = 0L;
        var normalInterval = Stopwatch.Frequency * 8333 / 1_000_000;
        var longInterval = Stopwatch.Frequency / 10;
        for (var frame = 0; frame < 600; frame++)
        {
            tracker.Record(timestamp);
            timestamp += frame == 550 ? longInterval : normalInterval;
        }

        var metrics = tracker.Capture();

        Assert.Equal(600UL, metrics.TotalPresentedFrames);
        Assert.Equal(512, metrics.WindowFrameCount);
        Assert.InRange(metrics.P95FrameInterval.TotalMilliseconds, 8.2, 8.5);
        Assert.InRange(metrics.MaximumFrameInterval.TotalMilliseconds, 99.9, 100.1);
    }

    [AvaloniaFact]
    public async Task Layout_invalidation_during_attachment_does_not_abandon_compositor_probe()
    {
        var precedingDetach = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var lifetime = new ViewportPresentationLifetime();
        var control = new ViewportCompositionControl(precedingDetach.Task)
        {
            Lifetime = lifetime,
        };
        var window = new Window
        {
            Width = 640,
            Height = 480,
            Content = control,
        };

        try
        {
            window.Show();
            control.RevisionToken = 1;
            window.Width = 800;
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(ViewportPresentationState.Detached, control.State);
            precedingDetach.SetResult(true);

            for (var attempt = 0;
                 attempt < 10 && control.State == ViewportPresentationState.Detached;
                 attempt++)
            {
                Dispatcher.UIThread.RunJobs();
                await Task.Yield();
            }

            Assert.NotEqual(ViewportPresentationState.Detached, control.State);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
            await lifetime.StopAndDrainAsync();
        }
    }

    private static ViewportPresentationFrame Frame(
        ViewportSessionId sessionId,
        Guid targetId,
        ulong revision,
        ulong sequence) => new(
        sessionId,
        ViewportTargetKind.DocumentScene,
        targetId,
        revision,
        sequence);

    private static ViewportSessionSnapshot Snapshot(
        ViewportSessionId sessionId,
        Guid targetId,
        ulong revision) => new(
        sessionId,
        ViewportRenderKind.Scene,
        ViewportTargetKind.DocumentScene,
        targetId,
        revision,
        LastSequence: 0,
        IsFrameInFlight: false,
        PendingReasons: ViewportInvalidationReason.None,
        IsClosed: false);

    private static void RecordCadence(
        ViewportPresentationCadenceTracker tracker,
        int framesPerSecond,
        int frameCount)
    {
        for (var index = 0; index < frameCount; index++)
        {
            tracker.Record(index * Stopwatch.Frequency / framesPerSecond);
        }
    }
}
