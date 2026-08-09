using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
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
        Assert.False(state.Synchronize(sizeA));

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
    public void Invalidating_surface_identity_hides_the_current_extent_and_advances_generation()
    {
        var extent = new ViewportExtent(640, 480);
        var state = new ViewportGeometryGenerationState();
        state.Synchronize(extent);
        state.MarkSurfaceUpdate(extent, state.CurrentGeneration);
        var presentedGeneration = state.CurrentGeneration;

        state.InvalidateSurface();

        Assert.Equal(extent, state.CurrentExtent);
        Assert.True(state.CurrentGeneration > presentedGeneration);
        Assert.False(state.HasExactSurface);
        Assert.Throws<InvalidOperationException>(
            () => state.MarkSurfaceUpdate(extent, presentedGeneration));
    }

    [Fact]
    public void Presentation_layout_probe_records_extent_without_advancing_front_geometry()
    {
        var frontExtent = new ViewportExtent(640, 480);
        var probedExtent = new ViewportExtent(800, 480);
        var geometry = new ViewportGeometryGenerationState();
        geometry.Synchronize(frontExtent);
        geometry.MarkSurfaceUpdate(frontExtent, geometry.CurrentGeneration);
        var frontGeneration = geometry.CurrentGeneration;
        var resize = new ViewportPresentationPreparationState();

        var probe = resize.BeginLayoutProbe();
        resize.ObserveLayoutProbe(probedExtent);

        Assert.True(resize.TryGetLayoutProbeExtent(probe, out var captured));
        Assert.Equal(probedExtent, captured);
        Assert.Equal(frontExtent, geometry.CurrentExtent);
        Assert.Equal(frontGeneration, geometry.CurrentGeneration);
        Assert.True(geometry.HasExactSurface);

        resize.EndLayoutProbe(probe);
        Assert.False(resize.TryGetLayoutProbeExtent(probe, out _));
    }

    [Fact]
    public void Failed_presentation_preparation_cannot_mutate_or_reactivate_front_geometry()
    {
        var frontExtent = new ViewportExtent(640, 480);
        var targetExtent = new ViewportExtent(800, 480);
        var geometry = new ViewportGeometryGenerationState();
        geometry.Synchronize(frontExtent);
        geometry.MarkSurfaceUpdate(frontExtent, geometry.CurrentGeneration);
        var frontGeneration = geometry.CurrentGeneration;
        var resize = new ViewportPresentationPreparationState();
        var ticket = resize.BeginPreparation(targetExtent, frontGeneration);

        Assert.True(resize.TryCancel(ticket));
        Assert.False(resize.TryMarkPrepared(ticket));
        Assert.False(resize.TryArm(ticket));
        Assert.Equal(frontExtent, geometry.SurfaceExtent);
        Assert.Equal(frontGeneration, geometry.SurfaceGeneration);
        Assert.True(geometry.HasExactSurface);
    }

    [Fact]
    public void Armed_presentation_requires_explicit_group_completion_after_exact_layout()
    {
        var frontExtent = new ViewportExtent(640, 480);
        var targetExtent = new ViewportExtent(800, 480);
        var resize = new ViewportPresentationPreparationState();
        var exactTicket = resize.BeginPreparation(targetExtent, baseGeometryGeneration: 4);
        Assert.True(resize.TryMarkPrepared(exactTicket));
        Assert.True(resize.TryArm(exactTicket));

        Assert.Equal(
            ViewportPresentationLayoutDisposition.ArmedExact,
            resize.ObserveBounds(targetExtent, out var consumedExact));
        Assert.Equal(exactTicket, consumedExact);
        Assert.True(resize.HasPreparation);
        Assert.True(resize.IsArmedExtentExact(exactTicket));
        Assert.True(resize.TryCompleteArmed(exactTicket));
        Assert.False(resize.HasPreparation);

        var mismatchTicket = resize.BeginPreparation(targetExtent, baseGeometryGeneration: 4);
        Assert.True(resize.TryMarkPrepared(mismatchTicket));
        Assert.True(resize.TryArm(mismatchTicket));
        Assert.Equal(
            ViewportPresentationLayoutDisposition.ArmedMismatch,
            resize.ObserveBounds(frontExtent, out var consumedMismatch));
        Assert.Equal(mismatchTicket, consumedMismatch);
        Assert.True(resize.HasPreparation);
        Assert.False(resize.IsArmedExtentExact(mismatchTicket));
        Assert.False(resize.TryCompleteArmed(mismatchTicket));
        Assert.True(resize.TryCancel(mismatchTicket));
        Assert.False(resize.HasPreparation);
    }

    [Theory]
    [InlineData(true, false, false, true)]
    [InlineData(true, true, true, true)]
    [InlineData(true, true, false, false)]
    [InlineData(false, false, false, false)]
    [InlineData(false, true, true, false)]
    public void Automatic_realtime_starts_a_candidate_or_requires_its_promotion(
        bool isRealtime,
        bool hasDesiredStream,
        bool desiredStreamIsPromoted,
        bool expected)
    {
        Assert.Equal(
            expected,
            ViewportRealtimeAdmissionPolicy.ShouldInvalidate(
                isRealtime,
                hasDesiredStream,
                desiredStreamIsPromoted));
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
    public void Consumer_access_without_a_final_visible_publish_cannot_mark_presented()
    {
        Assert.False(CompositionUpdatePresentationPolicy.CanMarkPresented(
            CompositionUpdateCompletion.ConsumerAccessed,
            finalCanPresent: true));
        Assert.False(CompositionUpdatePresentationPolicy.CanMarkPresented(
            CompositionUpdateCompletion.VisibleSurfacePublished,
            finalCanPresent: false));
        Assert.True(CompositionUpdatePresentationPolicy.CanMarkPresented(
            CompositionUpdateCompletion.VisibleSurfacePublished,
            finalCanPresent: true));
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
    public void Frame_before_the_current_snapshot_fence_is_rejected_at_the_same_document_revision()
    {
        var epoch = 13UL;
        var sessionId = ViewportSessionId.Create();
        var targetId = Guid.NewGuid();
        var current = Snapshot(
            sessionId,
            targetId,
            revision: 8,
            minimumPresentableSequence: 2);
        var state = new ViewportFramePresentationState();
        state.Reset(epoch);

        Assert.False(state.CanPresent(
            epoch,
            Frame(sessionId, targetId, revision: 8, sequence: 1),
            current));
        Assert.True(state.CanPresent(
            epoch,
            Frame(sessionId, targetId, revision: 8, sequence: 2),
            current));
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

    [Fact]
    public void Resize_metrics_count_each_geometry_generation_once_and_union_hidden_time()
    {
        var tracker = new ViewportGeometryDiagnosticsTracker();
        tracker.MarkRequestedVisualHidden(false, TimestampAtMilliseconds(0));
        var token = tracker.BeginMeasurement(
            baselineGeneration: 0,
            TimestampAtMilliseconds(0));

        tracker.MarkRequestedVisualHidden(true, TimestampAtMilliseconds(0));
        tracker.RecordGeneration(
            1,
            new ViewportExtent(640, 480),
            ViewportGeometryChangeSource.Bounds,
            TimestampAtMilliseconds(0));
        tracker.MarkExactSurfaceSubmitted(1, TimestampAtMilliseconds(5));
        tracker.MarkRequestedVisualHidden(false, TimestampAtMilliseconds(5));
        tracker.MarkExactSurfaceCompleted(1, TimestampAtMilliseconds(10));

        tracker.MarkRequestedVisualHidden(true, TimestampAtMilliseconds(20));
        tracker.RecordGeneration(
            2,
            new ViewportExtent(800, 480),
            ViewportGeometryChangeSource.Bounds,
            TimestampAtMilliseconds(20));
        tracker.MarkExactSurfaceSubmitted(2, TimestampAtMilliseconds(25));
        tracker.MarkRequestedVisualHidden(false, TimestampAtMilliseconds(25));
        tracker.MarkExactSurfaceCompleted(2, TimestampAtMilliseconds(35));

        tracker.MarkRequestedVisualHidden(true, TimestampAtMilliseconds(40));
        tracker.RecordGeneration(
            3,
            new ViewportExtent(640, 480),
            ViewportGeometryChangeSource.Bounds,
            TimestampAtMilliseconds(40));
        tracker.MarkExactSurfaceSubmitted(3, TimestampAtMilliseconds(45));
        tracker.MarkRequestedVisualHidden(false, TimestampAtMilliseconds(45));
        tracker.MarkExactSurfaceCompleted(3, TimestampAtMilliseconds(50));
        tracker.MarkExactSurfaceSubmitted(3, TimestampAtMilliseconds(55));
        tracker.MarkExactSurfaceCompleted(3, TimestampAtMilliseconds(60));

        var metrics = tracker.Capture(
            token,
            finalGeometryGeneration: 3,
            finalGenerationHasExactSurface: true,
            TimestampAtMilliseconds(50));

        Assert.Equal(3, metrics.ObservedBoundsGenerations);
        Assert.Equal(3, metrics.UniqueExactSubmittedGenerations);
        Assert.Equal(3, metrics.UniqueExactCompletedGenerations);
        Assert.Equal(1, metrics.CompletionCoverage);
        Assert.InRange(metrics.UniqueExactCompletedPerSecond, 59.9, 60.1);
        Assert.InRange(metrics.P95UniqueCompletionInterval.TotalMilliseconds, 24.9, 25.1);
        Assert.InRange(metrics.MaximumUniqueCompletionInterval.TotalMilliseconds, 24.9, 25.1);
        Assert.InRange(metrics.P95BoundsToExactSubmit.TotalMilliseconds, 4.9, 5.1);
        Assert.InRange(metrics.P95BoundsToExactCompletion.TotalMilliseconds, 14.9, 15.1);
        Assert.InRange(metrics.RequestedMismatchHiddenDuration.TotalMilliseconds, 14.9, 15.1);
        Assert.InRange(metrics.RequestedMismatchHiddenDutyCycle, 0.299, 0.301);
        Assert.True(metrics.FinalGenerationHasExactSurface);
        Assert.True(metrics.FinalGenerationCompleted);
        Assert.False(metrics.ContainsNonBoundsGeometryChanges);
        Assert.False(metrics.RingOverflowed);
    }

    [Fact]
    public void Resize_metrics_reject_a_window_mixed_with_a_scaling_generation()
    {
        var tracker = new ViewportGeometryDiagnosticsTracker();
        var token = tracker.BeginMeasurement(0, TimestampAtMilliseconds(0));
        tracker.RecordGeneration(
            1,
            new ViewportExtent(640, 480),
            ViewportGeometryChangeSource.Bounds,
            TimestampAtMilliseconds(1));
        tracker.RecordGeneration(
            2,
            new ViewportExtent(1280, 960),
            ViewportGeometryChangeSource.Scaling,
            TimestampAtMilliseconds(2));
        tracker.MarkExactSurfaceSubmitted(2, TimestampAtMilliseconds(3));
        tracker.MarkExactSurfaceCompleted(2, TimestampAtMilliseconds(4));

        var metrics = tracker.Capture(
            token,
            finalGeometryGeneration: 2,
            finalGenerationHasExactSurface: true,
            TimestampAtMilliseconds(4));

        Assert.True(metrics.ContainsNonBoundsGeometryChanges);
    }

    [Fact]
    public void Resize_metrics_reject_a_measurement_that_crosses_tracker_reset()
    {
        var tracker = new ViewportGeometryDiagnosticsTracker();
        var token = tracker.BeginMeasurement(0, TimestampAtMilliseconds(0));

        tracker.Reset();
        tracker.RecordGeneration(
            1,
            new ViewportExtent(640, 480),
            ViewportGeometryChangeSource.Bounds,
            TimestampAtMilliseconds(1));
        var metrics = tracker.Capture(
            token,
            finalGeometryGeneration: 1,
            finalGenerationHasExactSurface: false,
            TimestampAtMilliseconds(2));

        Assert.True(metrics.TrackerResetSinceMeasurement);
        Assert.Equal(0, metrics.ObservedBoundsGenerations);
    }

    [Fact]
    public void Resize_metrics_report_ring_overflow_instead_of_using_a_truncated_window()
    {
        var tracker = new ViewportGeometryDiagnosticsTracker();
        var token = tracker.BeginMeasurement(0, TimestampAtMilliseconds(0));
        for (var index = 0; index <= ViewportGeometryDiagnosticsTracker.RecordCapacity; index++)
        {
            tracker.RecordGeneration(
                checked((ulong)index + 1),
                new ViewportExtent(checked((uint)index + 1), 480),
                ViewportGeometryChangeSource.Bounds,
                TimestampAtMilliseconds(index));
        }

        var metrics = tracker.Capture(
            token,
            checked((ulong)ViewportGeometryDiagnosticsTracker.RecordCapacity + 1),
            finalGenerationHasExactSurface: false,
            TimestampAtMilliseconds(ViewportGeometryDiagnosticsTracker.RecordCapacity + 1));

        Assert.True(metrics.RingOverflowed);
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

    [AvaloniaFact]
    public async Task Ancestor_reexposure_requests_a_frame_for_a_clean_on_demand_session()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "viewport-refresh-test.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            ViewportCameraSnapshot.DefaultScene);
        var size = new ViewportRenderSize(
            new ViewportExtent(640, 480),
            new ViewportExtent(640, 480));
        Assert.True(session.TryPublishLatest(size, out _));

        var lifetime = new ViewportPresentationLifetime();
        var control = new ViewportCompositionControl
        {
            Session = session,
            Lifetime = lifetime,
            IsRealtime = false,
        };
        var host = new Border { Child = control };
        var window = new Window
        {
            Width = 640,
            Height = 480,
            Content = host,
        };

        try
        {
            window.Show();
            for (var attempt = 0;
                 attempt < 10 && control.State == ViewportPresentationState.Detached;
                 attempt++)
            {
                Dispatcher.UIThread.RunJobs();
                await Task.Yield();
            }
            while (session.TryPublishLatest(size, out _))
            {
            }

            var refreshRequests = 0;
            session.RefreshRequested += (_, _) => refreshRequests++;
            host.IsVisible = false;
            Dispatcher.UIThread.RunJobs();
            host.IsVisible = true;
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(1, refreshRequests);
            Assert.True(session.Current.PendingReasons.HasFlag(
                ViewportInvalidationReason.Exposed));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
            await lifetime.StopAndDrainAsync();
            session.Close();
        }
    }

    [AvaloniaFact]
    public async Task Session_swap_exposes_a_clean_on_demand_session()
    {
        static ViewportSession CreateSession() => new(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "viewport-session-swap.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            ViewportCameraSnapshot.DefaultScene);

        var firstSession = CreateSession();
        var nextSession = CreateSession();
        var size = new ViewportRenderSize(
            new ViewportExtent(640, 480),
            new ViewportExtent(640, 480));
        Assert.True(firstSession.TryPublishLatest(size, out _));
        Assert.True(nextSession.TryPublishLatest(size, out _));

        var lifetime = new ViewportPresentationLifetime();
        var control = new ViewportCompositionControl
        {
            Session = firstSession,
            Lifetime = lifetime,
            IsRealtime = false,
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
            for (var attempt = 0;
                 attempt < 10 && control.State == ViewportPresentationState.Detached;
                 attempt++)
            {
                Dispatcher.UIThread.RunJobs();
                await Task.Yield();
            }
            while (firstSession.TryPublishLatest(size, out _))
            {
            }

            var refreshRequests = 0;
            nextSession.RefreshRequested += (_, _) => refreshRequests++;
            control.Session = nextSession;
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(1, refreshRequests);
            Assert.True(nextSession.Current.PendingReasons.HasFlag(
                ViewportInvalidationReason.Exposed));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
            await lifetime.StopAndDrainAsync();
            firstSession.Close();
            nextSession.Close();
        }
    }

    [AvaloniaFact]
    public async Task Collapsing_bounds_does_not_invalidate_a_closed_session()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "viewport-closed-resize.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            ViewportCameraSnapshot.DefaultScene);
        var lifetime = new ViewportPresentationLifetime();
        var control = new ViewportCompositionControl
        {
            Session = session,
            Lifetime = lifetime,
            IsRealtime = false,
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
            Dispatcher.UIThread.RunJobs();
            session.Close();

            control.Arrange(new Rect(0, 0, 0, 0));
            Dispatcher.UIThread.RunJobs();
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
            await lifetime.StopAndDrainAsync();
            session.Close();
        }
    }

    [AvaloniaFact]
    public async Task Lifetime_replacement_exposes_a_clean_on_demand_session()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "viewport-lifetime-replacement.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            ViewportCameraSnapshot.DefaultScene);
        var size = new ViewportRenderSize(
            new ViewportExtent(640, 480),
            new ViewportExtent(640, 480));
        var firstLifetime = new ViewportPresentationLifetime();
        var nextLifetime = new ViewportPresentationLifetime();
        var control = new ViewportCompositionControl
        {
            Session = session,
            Lifetime = firstLifetime,
            IsRealtime = false,
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
            Dispatcher.UIThread.RunJobs();
            while (session.TryPublishLatest(size, out _))
            {
            }

            var refreshRequests = 0;
            session.RefreshRequested += (_, _) => refreshRequests++;
            control.Lifetime = nextLifetime;
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(1, refreshRequests);
            Assert.True(session.Current.PendingReasons.HasFlag(
                ViewportInvalidationReason.Exposed));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
            await firstLifetime.StopAndDrainAsync();
            await nextLifetime.StopAndDrainAsync();
            session.Close();
        }
    }

    [AvaloniaFact]
    public async Task Ancestor_reexposure_does_not_invalidate_a_closed_session()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "viewport-closed-exposure.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            ViewportCameraSnapshot.DefaultScene);
        var lifetime = new ViewportPresentationLifetime();
        var control = new ViewportCompositionControl
        {
            Session = session,
            Lifetime = lifetime,
            IsRealtime = false,
        };
        var host = new Border { Child = control };
        var window = new Window
        {
            Width = 640,
            Height = 480,
            Content = host,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            session.Close();

            host.IsVisible = false;
            Dispatcher.UIThread.RunJobs();
            host.IsVisible = true;
            Dispatcher.UIThread.RunJobs();
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
            await lifetime.StopAndDrainAsync();
            session.Close();
        }
    }

    [AvaloniaFact]
    public async Task Lifetime_resume_exposes_a_clean_on_demand_session()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "viewport-lifetime-resume.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
            ViewportCameraSnapshot.DefaultScene);
        var size = new ViewportRenderSize(
            new ViewportExtent(640, 480),
            new ViewportExtent(640, 480));
        var lifetime = new ViewportPresentationLifetime();
        var control = new ViewportCompositionControl
        {
            Session = session,
            Lifetime = lifetime,
            IsRealtime = false,
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
            Dispatcher.UIThread.RunJobs();
            while (session.TryPublishLatest(size, out _))
            {
            }

            var pause = await lifetime.PauseAndDrainAsync();
            var refreshRequests = 0;
            session.RefreshRequested += (_, _) => refreshRequests++;
            await pause.DisposeAsync();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(1, refreshRequests);
            Assert.True(session.Current.PendingReasons.HasFlag(
                ViewportInvalidationReason.Exposed));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
            await lifetime.StopAndDrainAsync();
            session.Close();
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
        ulong revision,
        ulong minimumPresentableSequence = 1) => new(
        sessionId,
        ViewportRenderKind.Scene,
        ViewportTargetKind.DocumentScene,
        targetId,
        revision,
        LastSequence: 0,
        MinimumPresentableSequence: minimumPresentableSequence,
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

    private static long TimestampAtMilliseconds(double milliseconds) => checked(
        (long)Math.Round(milliseconds * Stopwatch.Frequency / 1000.0));
}
