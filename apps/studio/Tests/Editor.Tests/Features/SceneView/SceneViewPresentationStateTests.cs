using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Viewports;
using Avalonia;
using Editor.Features.SceneView.Interop;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewPresentationStateTests
{
    private static readonly ViewportId ViewportId = new("scene-view/test");

    [Fact]
    public void Observation_preserves_exact_display_size_and_derives_pixel_extent_once()
    {
        var observation =
            SceneViewFrameObservation.TryCreate(
                ViewportId,
                new Size(100.25, 70.5),
                renderScale: 1.5,
                hasScene: true,
                sceneRevision: 12UL);

        Assert.NotNull(observation);
        Assert.Equal(new Size(100.25, 70.5), observation.DisplaySizeDip);
        Assert.Equal(151, observation.PixelExtent.WidthPixels);
        Assert.Equal(106, observation.PixelExtent.HeightPixels);
        Assert.Equal(1.5, observation.PixelExtent.RenderScale);
    }

    [Fact]
    public void Consecutive_resize_presents_completed_frames_monotonically_while_C_remains_latest()
    {
        var state = CreateAttachedState();
        var requestA = state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var workA));
        state.CompleteSlotCreation(workA.SlotId);

        var requestB = state.Observe(Observation(width: 200));
        var requestC = state.Observe(Observation(width: 300));

        Assert.False(state.IsCurrent(requestA));
        Assert.False(state.IsCurrent(requestB));
        Assert.True(state.IsCurrent(requestC));
        Assert.Equal(requestC, state.LatestPendingRequest);
        Assert.True(state.CanPresent(requestA));
        Assert.True(state.MarkPresented(requestA));
        Assert.True(state.CanPresent(requestB));
        Assert.True(state.MarkPresented(requestB));
        Assert.False(state.CanPresent(requestA));
        Assert.True(state.CanPresent(requestC));
        Assert.True(state.MarkPresented(requestC));
        Assert.Equal(
            requestC.FrameSequence,
            state.LastPresentedFrameSequence);

        state.CompleteFrame(
            workA.SlotId,
            canReuse: false);
        _ = state.CollectRetirements();

        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var latestWork));
        Assert.Equal(requestC, latestWork.Request);
    }

    [Fact]
    public void Newer_presented_resize_prevents_a_late_intermediate_frame_from_regressing()
    {
        var state = CreateAttachedState();
        var requestA = state.Observe(Observation(width: 100));
        var requestB = state.Observe(Observation(width: 200));
        var requestC = state.Observe(Observation(width: 300));

        Assert.True(state.MarkPresented(requestA));
        Assert.True(state.MarkPresented(requestC));
        Assert.False(state.CanPresent(requestB));
        Assert.False(state.MarkPresented(requestB));
        Assert.Equal(
            requestC.FrameSequence,
            state.LastPresentedFrameSequence);
    }

    [Fact]
    public void Scene_revision_change_rejects_completed_frames_from_the_old_content()
    {
        var state = CreateAttachedState();
        var oldScene =
            state.Observe(
                Observation(
                    width: 100,
                    sceneRevision: 4));
        var resizedOldScene =
            state.Observe(
                Observation(
                    width: 200,
                    sceneRevision: 4));

        Assert.True(state.CanPresent(oldScene));
        Assert.True(state.MarkPresented(oldScene));
        Assert.True(state.CanPresent(resizedOldScene));

        var newScene =
            state.Observe(
                Observation(
                    width: 200,
                    sceneRevision: 5));

        Assert.False(state.CanPresent(resizedOldScene));
        Assert.False(state.MarkPresented(resizedOldScene));
        Assert.True(state.CanPresent(newScene));
        Assert.True(state.MarkPresented(newScene));
    }

    [Fact]
    public async Task Superseded_work_waiting_for_native_start_yields_to_latest_request()
    {
        var state = CreateAttachedState();
        state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var workA));

        state.Observe(Observation(width: 200));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var workB));
        using var producerGate = new SemaphoreSlim(initialCount: 0, maxCount: 1);
        var queuedNativeStart =
            producerGate.WaitAsync(
                workB.NativeStartAdmission.CancellationToken);

        var requestC = state.Observe(Observation(width: 300));

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            async () => await queuedNativeStart);
        Assert.True(workA.NativeStartAdmission.WasCanceled);
        Assert.True(workB.NativeStartAdmission.WasCanceled);

        state.AbortWork(workB.SlotId);
        Assert.True(workB.NativeStartAdmission.IsDisposed);
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var latestWork));
        Assert.Equal(requestC, latestWork.Request);

        state.AbortWork(workA.SlotId);
        state.AbortWork(latestWork.SlotId);
    }

    [Fact]
    public void Superseding_started_native_work_does_not_cancel_its_admission()
    {
        var state = CreateAttachedState();
        state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var startedWork));
        Assert.True(startedWork.NativeStartAdmission.TryBegin());

        state.Observe(Observation(width: 200));

        Assert.False(startedWork.NativeStartAdmission.WasCanceled);
        Assert.False(
            startedWork.NativeStartAdmission.CancellationToken
                .IsCancellationRequested);

        state.CompleteFrame(
            startedWork.SlotId,
            canReuse: false);
        Assert.True(startedWork.NativeStartAdmission.IsDisposed);
    }

    [Fact]
    public void Successful_first_frame_stops_without_automatic_warmup()
    {
        var state = CreateAttachedState();

        state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var first));
        Assert.Equal(SceneViewPresentationWorkKind.CreateSlot, first.Kind);
        state.CompleteSlotCreation(first.SlotId);
        state.CompleteFrame(
            first.SlotId,
            canReuse: true);

        Assert.Null(state.LatestPendingRequest);
        Assert.False(
            state.TryBeginWork(allowSlotCreation: true, out _));
        Assert.Equal(1, state.ActiveSlotCount);
    }

    [Fact]
    public void First_real_follow_up_request_creates_the_second_slot_lazily()
    {
        var state = CreateAttachedState();
        state.Observe(Observation(width: 100, sceneRevision: 1));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var first));
        state.CompleteSlotCreation(first.SlotId);
        state.CompleteFrame(first.SlotId, canReuse: true);

        var followUp =
            state.Observe(
                Observation(
                    width: 100,
                    sceneRevision: 2));

        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var second));
        Assert.Equal(SceneViewPresentationWorkKind.CreateSlot, second.Kind);
        Assert.Equal(followUp, second.Request);
        Assert.NotEqual(first.SlotId, second.SlotId);
        Assert.Equal(2, state.ActiveSlotCount);
        state.AbortWork(second.SlotId);
    }

    [Fact]
    public void Identical_observation_does_not_supersede_an_in_flight_frame()
    {
        var state = CreateAttachedState();
        var firstRequest = state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var firstWork));
        state.CompleteSlotCreation(firstWork.SlotId);

        var duplicateRequest = state.Observe(Observation(width: 100));

        Assert.Equal(firstRequest, duplicateRequest);
        Assert.True(state.IsCurrent(firstRequest));
        Assert.Null(state.LatestPendingRequest);
        Assert.False(firstWork.NativeStartAdmission.WasCanceled);

        state.CompleteFrame(
            firstWork.SlotId,
            canReuse: true);
        var nextRequest = state.Observe(Observation(width: 100));

        Assert.True(nextRequest.FrameSequence > firstRequest.FrameSequence);
        Assert.Equal(nextRequest, state.LatestPendingRequest);
    }

    [Fact]
    public void Reset_cancels_work_waiting_for_native_start()
    {
        var state = CreateAttachedState();
        state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var work));

        state.Reset();

        Assert.True(work.NativeStartAdmission.WasCanceled);
        state.AbortWork(work.SlotId);
        Assert.True(work.NativeStartAdmission.IsDisposed);
    }

    [Fact]
    public void Detach_cancels_work_waiting_for_native_start()
    {
        var state = CreateAttachedState();
        state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var work));

        state.Detach();

        Assert.True(work.NativeStartAdmission.WasCanceled);
        state.AbortWork(work.SlotId);
        Assert.True(work.NativeStartAdmission.IsDisposed);
    }

    [Fact]
    public void Two_busy_slots_keep_latest_request_until_a_slot_is_released()
    {
        var state = CreateAttachedState();
        CreateTwoAvailableSlots(state, width: 100);

        state.Observe(Observation(width: 100, sceneRevision: 2));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var first));
        state.Observe(Observation(width: 100, sceneRevision: 3));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var second));

        var latest = state.Observe(Observation(width: 100, sceneRevision: 4));
        Assert.False(
            state.TryBeginWork(allowSlotCreation: true, out _));
        Assert.Equal(latest, state.LatestPendingRequest);

        state.CompleteFrame(
            first.SlotId,
            canReuse: false);
        _ = state.CollectRetirements();

        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var retried));
        Assert.Equal(SceneViewPresentationWorkKind.CreateSlot, retried.Kind);
        Assert.Equal(latest, retried.Request);
    }

    [Fact]
    public void Retiring_generations_leave_the_two_slot_active_chain_immediately()
    {
        var state = CreateAttachedState();
        CreateTwoAvailableSlots(state, width: 100);

        state.Observe(Observation(width: 200));
        var generationARetirements = state.CollectRetirements();
        Assert.Equal(2, generationARetirements.Count);
        Assert.Equal(0, state.ActiveSlotCount);
        CreateTwoAvailableSlots(state, width: 200);
        Assert.Equal(
            SceneViewPresentationState.MaximumActiveSlots,
            state.ActiveSlotCount);

        var current = state.Observe(Observation(width: 300));
        var generationBRetirements = state.CollectRetirements();
        Assert.Equal(2, generationBRetirements.Count);
        Assert.Equal(0, state.ActiveSlotCount);
        Assert.False(
            state.TryBeginWork(allowSlotCreation: false, out _));
        Assert.Equal(current, state.LatestPendingRequest);

        Assert.True(
            state.TryBeginWork(
                allowSlotCreation: true,
                out var generationC));
        Assert.Equal(SceneViewPresentationWorkKind.CreateSlot, generationC.Kind);
        Assert.True(
            state.ActiveSlotCount <=
            SceneViewPresentationState.MaximumActiveSlots);
    }

    [Fact]
    public void Retiring_slots_do_not_count_toward_the_generation_limit()
    {
        var state = CreateAttachedState();

        state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var first));
        state.CompleteSlotCreation(first.SlotId);
        state.CompleteFrame(
            first.SlotId,
            canReuse: false);
        Assert.Equal(0, state.ActiveSlotCount);
        Assert.Equal(1, state.PendingRetirementCount);

        state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var second));
        state.CompleteSlotCreation(second.SlotId);
        state.CompleteFrame(
            second.SlotId,
            canReuse: false);
        Assert.Equal(0, state.ActiveSlotCount);
        Assert.Equal(2, state.PendingRetirementCount);

        var latest = state.Observe(Observation(width: 100));

        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var third));
        Assert.Equal(latest, third.Request);
        Assert.Equal(SceneViewPresentationWorkKind.CreateSlot, third.Kind);
    }

    [Fact]
    public void Owned_capacity_gate_preserves_latest_but_allows_slot_reuse()
    {
        var state = CreateAttachedState();
        var firstRequest = state.Observe(Observation(width: 100));

        Assert.False(
            state.TryBeginWork(allowSlotCreation: false, out _));
        Assert.Equal(firstRequest, state.LatestPendingRequest);

        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var created));
        state.CompleteSlotCreation(created.SlotId);
        state.CompleteFrame(
            created.SlotId,
            canReuse: true);

        var secondRequest =
            state.Observe(Observation(width: 100, sceneRevision: 2));
        Assert.True(
            state.TryBeginWork(
                allowSlotCreation: false,
                out var reused));
        Assert.Equal(SceneViewPresentationWorkKind.RenderSlot, reused.Kind);
        Assert.Equal(secondRequest, reused.Request);
    }

    [Fact]
    public void Detach_invalidates_current_request_and_retires_available_slots()
    {
        var state = CreateAttachedState();
        state.Observe(Observation(width: 100));
        Assert.True(
            state.TryBeginWork(allowSlotCreation: true, out var work));
        state.CompleteSlotCreation(work.SlotId);
        state.CompleteFrame(
            work.SlotId,
            canReuse: true);

        var request = state.Observe(Observation(width: 100));
        var retirements = state.Detach();

        Assert.False(state.IsCurrent(request));
        Assert.Null(state.LatestPendingRequest);
        Assert.Single(retirements);
    }

    private static SceneViewPresentationState CreateAttachedState()
    {
        var state = new SceneViewPresentationState();
        state.Attach();
        return state;
    }

    private static SceneViewFrameObservation Observation(
        double width,
        ulong sceneRevision = 1UL)
    {
        return SceneViewFrameObservation.TryCreate(
            ViewportId,
            new Size(width, 80),
            renderScale: 1.25,
            hasScene: true,
            sceneRevision)!;
    }

    private static void CreateTwoAvailableSlots(
        SceneViewPresentationState state,
        double width)
    {
        for (var index = 0; index < 2; index++)
        {
            state.Observe(Observation(width));
            Assert.True(
                state.TryBeginWork(
                    allowSlotCreation: true,
                    out var work));
            Assert.Equal(SceneViewPresentationWorkKind.CreateSlot, work.Kind);
            state.CompleteSlotCreation(work.SlotId);
            state.CompleteFrame(
                work.SlotId,
                canReuse: true);
        }
    }
}
