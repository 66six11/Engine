using System;
using System.Linq;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Viewports;
using Xunit;

namespace Asharia.Studio.Application.Tests.Viewports;

public sealed class ViewportSessionTests
{
    [Fact]
    public void Default_camera_policy_is_explicit_for_each_viewport_kind()
    {
        Assert.Same(
            ViewportCameraSnapshot.DefaultScene,
            ViewportCameraSnapshot.DefaultFor(ViewportRenderKind.Scene));
        Assert.Equal(
            ViewportFieldOfViewAxis.MaintainHorizontal,
            ViewportCameraSnapshot.DefaultScene.FieldOfViewAxis);
        Assert.Equal(MathF.PI / 2, ViewportCameraSnapshot.DefaultScene.FieldOfViewRadians);

        Assert.Same(
            ViewportCameraSnapshot.DefaultGame,
            ViewportCameraSnapshot.DefaultFor(ViewportRenderKind.Game));
        Assert.Equal(
            ViewportFieldOfViewAxis.MaintainVertical,
            ViewportCameraSnapshot.DefaultGame.FieldOfViewAxis);
        Assert.Equal(MathF.PI / 3, ViewportCameraSnapshot.DefaultGame.FieldOfViewRadians);

        Assert.Same(
            ViewportCameraSnapshot.DefaultPreview,
            ViewportCameraSnapshot.DefaultFor(ViewportRenderKind.Preview));
        Assert.Equal(
            ViewportFieldOfViewAxis.MaintainVertical,
            ViewportCameraSnapshot.DefaultPreview.FieldOfViewAxis);
        Assert.Equal(MathF.PI / 3, ViewportCameraSnapshot.DefaultPreview.FieldOfViewRadians);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ViewportCameraSnapshot.DefaultFor((ViewportRenderKind)42));
    }

    [Fact]
    public void First_request_captures_document_camera_and_bounded_debug_proxies()
    {
        var document = Document(revision: 7, entityCount: ViewportRenderRequest.MaximumDebugProxyCount + 2);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);

        var renderSize = new ViewportRenderSize(
            new ViewportExtent(1277, 719),
            new ViewportExtent(1280, 720));
        var started = session.TryBeginRender(renderSize, out var request);

        Assert.True(started);
        Assert.Equal(1UL, request.Sequence);
        Assert.Equal(document.SceneId, request.TargetId);
        Assert.Equal(document.Revision, request.TargetRevision);
        Assert.Equal(ViewportRenderKind.Scene, request.Kind);
        Assert.Equal(ViewportTargetKind.DocumentScene, request.TargetKind);
        Assert.Equal(renderSize, request.RenderSize);
        Assert.Equal(renderSize.LogicalExtent, request.LogicalExtent);
        Assert.Equal(renderSize.AllocationExtent, request.AllocationExtent);
        Assert.Equal(ViewportCameraSnapshot.DefaultScene, request.Camera);
        Assert.Equal(ViewportFieldOfViewAxis.MaintainHorizontal, request.Camera.FieldOfViewAxis);
        Assert.Equal(MathF.PI / 2, request.Camera.FieldOfViewRadians);
        Assert.True(request.Reasons.HasFlag(ViewportInvalidationReason.InitialFrame));
        Assert.True(request.Reasons.HasFlag(ViewportInvalidationReason.ExtentChanged));
        Assert.Equal(ViewportRenderRequest.MaximumDebugProxyCount, request.DebugProxies.Count);
        Assert.Equal(document.Entities.Count, request.TotalDebugProxyCount);
        Assert.True(request.DebugProxiesTruncated);
        Assert.Equal(document.Entities[0].ObjectId, request.DebugProxies[0].ObjectId);
        Assert.Equal(document.Entities[0].Transform, request.DebugProxies[0].Transform);
    }

    [Fact]
    public void Vertical_extent_change_invalidates_projection_without_rewriting_camera_policy()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var firstExtent = new ViewportExtent(641, 481);
        var secondExtent = new ViewportExtent(641, 719);
        var firstSize = new ViewportRenderSize(
            firstExtent,
            firstExtent);
        var secondSize = new ViewportRenderSize(
            secondExtent,
            secondExtent);

        Assert.True(session.TryBeginRender(firstSize, out var first));
        Assert.True(session.CompleteRender(first.Sequence, first.TargetRevision, succeeded: true));
        Assert.True(session.TryBeginRender(secondSize, out var second));

        Assert.Equal(2UL, second.Sequence);
        Assert.Equal(secondSize, second.RenderSize);
        Assert.Same(first.Camera, second.Camera);
        Assert.Equal(ViewportFieldOfViewAxis.MaintainHorizontal, second.Camera.FieldOfViewAxis);
        Assert.Equal(MathF.PI / 2, second.Camera.FieldOfViewRadians);
        Assert.True(second.Reasons.HasFlag(ViewportInvalidationReason.ExtentChanged));
    }

    [Fact]
    public void Exact_extent_change_leaves_staleness_to_the_geometry_generation_gate()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var firstExtent = new ViewportExtent(641, 481);
        var secondExtent = new ViewportExtent(642, 481);

        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(firstExtent, firstExtent),
            out _));
        var minimumPresentableSequence = session.Current.MinimumPresentableSequence;
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(secondExtent, secondExtent),
            out var resized));

        Assert.Equal(minimumPresentableSequence, session.Current.MinimumPresentableSequence);
        Assert.True(resized.Reasons.HasFlag(ViewportInvalidationReason.ExtentChanged));
    }

    [Fact]
    public void Explicit_extent_invalidation_republishes_the_same_size_after_zero_layout()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);
        Assert.True(session.TryPublishLatest(size, out _));

        session.Invalidate(ViewportInvalidationReason.ExtentChanged);

        Assert.True(session.TryPublishLatest(size, out var restored));
        Assert.Equal(size, restored.RenderSize);
        Assert.True(restored.Reasons.HasFlag(ViewportInvalidationReason.ExtentChanged));
    }

    [Fact]
    public void Realtime_invalidation_produces_one_latest_frame_request()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var size = new ViewportRenderSize(
            new ViewportExtent(640, 360),
            new ViewportExtent(640, 384));

        Assert.True(session.TryPublishLatest(size, out _));
        session.Invalidate(ViewportInvalidationReason.Realtime);
        session.Invalidate(ViewportInvalidationReason.Realtime);
        Assert.True(session.TryPublishLatest(size, out var realtime));
        Assert.Equal(ViewportInvalidationReason.Realtime, realtime.Reasons);
        Assert.False(session.TryPublishLatest(size, out _));
    }

    [Fact]
    public void Realtime_invalidation_does_not_reject_an_already_published_snapshot()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);

        Assert.True(session.TryPublishLatest(size, out var first));
        var minimumPresentableSequence = session.Current.MinimumPresentableSequence;

        session.Invalidate(ViewportInvalidationReason.Realtime);

        Assert.Equal(minimumPresentableSequence, session.Current.MinimumPresentableSequence);
        Assert.True(session.CanPresentPublishedFrame(first.Sequence, first.TargetRevision));
    }

    [Fact]
    public void Document_changes_during_a_frame_are_coalesced_after_stale_completion()
    {
        var original = Document(revision: 2, entityCount: 1);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            original,
            ViewportCameraSnapshot.DefaultScene);
        Assert.True(session.TryBeginRender(new ViewportExtent(640, 360), out var first));

        var updated = new SceneDocumentSnapshot(
            original.SceneId,
            original.Path,
            revision: 3,
            savedRevision: 2,
            original.Entities.Select(entity => new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                entity.Name,
                new TransformValue(new Float3(3, 2, 1), Quaternion.Identity, Float3.One))));
        session.SynchronizeDocument(updated);

        Assert.False(session.CompleteRender(first.Sequence, first.TargetRevision, succeeded: true));
        Assert.True(session.TryBeginRender(new ViewportExtent(640, 360), out var second));
        Assert.Equal(2UL, second.Sequence);
        Assert.Equal(3UL, second.TargetRevision);
        Assert.Equal(new Float3(3, 2, 1), second.DebugProxies.Single().Transform.Position);
        Assert.Equal(ViewportInvalidationReason.TargetChanged, second.Reasons);
        Assert.True(session.CompleteRender(second.Sequence, second.TargetRevision, succeeded: true));
    }

    [Fact]
    public void Session_allows_only_one_in_flight_request_and_keeps_instances_independent()
    {
        var document = Document(revision: 4, entityCount: 0);
        var firstSession = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        var secondSession = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Game,
            document,
            ViewportCameraSnapshot.DefaultGame);

        Assert.True(firstSession.TryBeginRender(new ViewportExtent(800, 450), out var first));
        Assert.False(firstSession.TryBeginRender(new ViewportExtent(800, 450), out _));
        Assert.True(secondSession.TryBeginRender(new ViewportExtent(320, 180), out var second));

        Assert.NotEqual(first.SessionId, second.SessionId);
        Assert.Equal(1UL, first.Sequence);
        Assert.Equal(1UL, second.Sequence);
        Assert.Equal(ViewportRenderKind.Game, second.Kind);
        Assert.Equal(ViewportFieldOfViewAxis.MaintainVertical, second.Camera.FieldOfViewAxis);
        Assert.True(firstSession.CompleteRender(first.Sequence, first.TargetRevision, succeeded: true));
        Assert.True(secondSession.CompleteRender(second.Sequence, second.TargetRevision, succeeded: true));

        var updated = new SceneDocumentSnapshot(
            document.SceneId,
            document.Path,
            revision: 5,
            savedRevision: 1,
            document.Entities);
        var movedCamera = new ViewportCameraSnapshot(
            new Float3(4, 3, -8),
            Float3.Zero,
            new Float3(0, 1, 0),
            MathF.PI / 2,
            ViewportFieldOfViewAxis.MaintainHorizontal,
            0.1f,
            1000.0f);
        firstSession.SynchronizeDocument(updated);
        firstSession.SetCamera(movedCamera);

        Assert.True(firstSession.TryBeginRender(new ViewportExtent(800, 450), out var changed));
        Assert.Equal(2UL, changed.Sequence);
        Assert.Equal(5UL, changed.TargetRevision);
        Assert.Equal(movedCamera, changed.Camera);
        Assert.True(changed.Reasons.HasFlag(ViewportInvalidationReason.TargetChanged));
        Assert.True(changed.Reasons.HasFlag(ViewportInvalidationReason.CameraChanged));
        Assert.False(secondSession.TryBeginRender(new ViewportExtent(320, 180), out _));
    }

    [Fact]
    public void Camera_and_extent_changes_coalesce_without_continuous_scheduling()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Preview,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultPreview);
        Assert.True(session.TryBeginRender(new ViewportExtent(400, 300), out var first));
        Assert.True(session.CompleteRender(first.Sequence, first.TargetRevision, succeeded: true));
        Assert.False(session.TryBeginRender(new ViewportExtent(400, 300), out _));

        var changedCamera = new ViewportCameraSnapshot(
            new Float3(2, 3, -7),
            Float3.Zero,
            new Float3(0, 1, 0),
            MathF.PI / 3,
            ViewportFieldOfViewAxis.MaintainVertical,
            0.1f,
            1000.0f);
        session.SetCamera(changedCamera);

        Assert.True(session.TryBeginRender(new ViewportExtent(600, 400), out var changed));
        Assert.True(changed.Reasons.HasFlag(ViewportInvalidationReason.CameraChanged));
        Assert.True(changed.Reasons.HasFlag(ViewportInvalidationReason.ExtentChanged));
        Assert.Equal(changedCamera, changed.Camera);
    }

    [Fact]
    public void Camera_change_keeps_an_older_frame_presentable_until_the_latest_camera_is_published()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);
        Assert.True(session.TryPublishLatest(size, out var stale));

        var movedCamera = CameraAt(3);
        session.SetCamera(movedCamera);

        Assert.Equal(stale.Sequence, session.Current.MinimumPresentableSequence);
        Assert.True(session.CanPresentPublishedFrame(stale.Sequence, stale.TargetRevision));
        Assert.True(session.MarkPublishedFramePresented(stale.Sequence, stale.TargetRevision));
        Assert.False(session.CanUsePublishedFrameForInteraction(
            stale.Sequence,
            stale.TargetRevision,
            stale.ViewStateRevision));
        Assert.False(session.TryCapturePickSnapshot(
            stale.SessionId,
            stale.TargetId,
            stale.TargetRevision,
            stale.Sequence,
            out _));
        Assert.True(session.TryPublishLatest(size, out var current));
        Assert.Equal(movedCamera, current.Camera);
        Assert.True(session.CanPresentPublishedFrame(current.Sequence, current.TargetRevision));
        Assert.True(session.MarkPublishedFramePresented(current.Sequence, current.TargetRevision));
        Assert.True(session.CanUsePublishedFrameForInteraction(
            current.Sequence,
            current.TargetRevision,
            current.ViewStateRevision));
        Assert.True(session.TryCapturePickSnapshot(
            current.SessionId,
            current.TargetId,
            current.TargetRevision,
            current.Sequence,
            out var pickSnapshot));
        Assert.Equal(movedCamera, pickSnapshot.Camera);
    }

    [Fact]
    public void Continuous_camera_changes_do_not_starve_every_published_frame()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);
        Assert.True(session.TryPublishLatest(size, out var published));
        var presentationFence = session.Current.MinimumPresentableSequence;

        for (var inputIndex = 1; inputIndex <= 240; inputIndex++)
        {
            session.SetCamera(CameraAt(inputIndex));

            Assert.Equal(presentationFence, session.Current.MinimumPresentableSequence);
            Assert.True(session.CanPresentPublishedFrame(
                published.Sequence,
                published.TargetRevision));
            Assert.False(session.CanUsePublishedFrameForInteraction(
                published.Sequence,
                published.TargetRevision,
                published.ViewStateRevision));
            Assert.True(session.TryPublishLatest(size, out published));
        }

        Assert.Equal(CameraAt(240), published.Camera);
        Assert.True(session.CanPresentPublishedFrame(
            published.Sequence,
            published.TargetRevision));
        Assert.True(session.CanUsePublishedFrameForInteraction(
            published.Sequence,
            published.TargetRevision,
            published.ViewStateRevision));
    }

    [Fact]
    public void Selection_change_is_versioned_and_rejects_the_previous_view_state()
    {
        var document = Document(revision: 1, entityCount: 1);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);
        Assert.True(session.TryPublishLatest(size, out var stale));

        var selectedObjectId = document.Entities.Single().ObjectId;
        session.SetSelection(viewStateRevision: 1, selectedObjectId);

        Assert.False(session.CanPresentPublishedFrame(
            stale.Sequence,
            stale.TargetRevision,
            stale.ViewStateRevision));
        Assert.True(session.TryPublishLatest(size, out var selected));
        Assert.Equal(1UL, selected.ViewStateRevision);
        Assert.Equal(selectedObjectId, selected.SelectedObjectId);
        Assert.True(selected.Reasons.HasFlag(ViewportInvalidationReason.SelectionChanged));
        Assert.True(session.CanPresentPublishedFrame(
            selected.Sequence,
            selected.TargetRevision,
            selected.ViewStateRevision));

        session.SetSelection(viewStateRevision: 2, selectedObjectId: null);
        Assert.True(session.TryPublishLatest(size, out var cleared));
        Assert.Equal(2UL, cleared.ViewStateRevision);
        Assert.Null(cleared.SelectedObjectId);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            session.SetSelection(viewStateRevision: 1, selectedObjectId));
    }

    [Fact]
    public void Translate_gizmo_preview_is_non_fencing_and_uses_latest_transform()
    {
        var document = Document(revision: 1, entityCount: 1);
        var selectedObjectId = document.Entities.Single().ObjectId;
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);
        session.SetSelection(viewStateRevision: 1, selectedObjectId);
        Assert.True(session.TryPublishLatest(size, out var published));
        var presentationFence = session.Current.MinimumPresentableSequence;

        for (var inputIndex = 1; inputIndex <= 240; inputIndex++)
        {
            var preview = new TransformValue(
                new Float3(inputIndex, 2, 3),
                Quaternion.Identity,
                Float3.One);
            session.SetTransformGizmo(new ViewportTransformGizmoState(
                ViewportTransformGizmoKind.Translate,
                selectedObjectId,
                preview,
                ViewportGizmoAxis.X,
                ViewportGizmoAxis.X));

            Assert.Equal(presentationFence, session.Current.MinimumPresentableSequence);
            Assert.True(session.CanPresentPublishedFrame(
                published.Sequence,
                published.TargetRevision));
            Assert.True(session.TryPublishLatest(size, out published));
            Assert.Equal(preview, Assert.IsType<ViewportTransformGizmoState>(
                published.TransformGizmo).Transform);
            Assert.True(published.Reasons.HasFlag(ViewportInvalidationReason.GizmoChanged));
        }

        Assert.Equal(new Float3(240, 2, 3), published.TransformGizmo!.Transform.Position);
    }

    [Fact]
    public void Document_synchronization_restores_authoritative_gizmo_transform()
    {
        var document = Document(revision: 1, entityCount: 1);
        var entity = document.Entities.Single();
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);
        session.SetSelection(viewStateRevision: 1, entity.ObjectId);
        Assert.True(session.TryPublishLatest(size, out _));
        session.SetTransformGizmo(new ViewportTransformGizmoState(
            ViewportTransformGizmoKind.Translate,
            entity.ObjectId,
            new TransformValue(new Float3(9, 8, 7), Quaternion.Identity, Float3.One),
            ViewportGizmoAxis.X,
            ViewportGizmoAxis.X));

        var authoritative = new TransformValue(
            new Float3(4, 5, 6),
            Quaternion.Identity,
            Float3.One);
        session.SynchronizeDocument(new SceneDocumentSnapshot(
            document.SceneId,
            document.Path,
            revision: 2,
            savedRevision: 1,
            [
                new SceneEntitySnapshot(
                    entity.ObjectId,
                    entity.RuntimeEntityId,
                    entity.Name,
                    authoritative),
            ]));

        Assert.True(session.TryPublishLatest(size, out var synchronized));
        Assert.Equal(authoritative, synchronized.TransformGizmo!.Transform);
        Assert.Equal(ViewportGizmoAxis.None, synchronized.TransformGizmo.HoveredAxis);
        Assert.Equal(ViewportGizmoAxis.None, synchronized.TransformGizmo.ActiveAxis);
    }

    [Fact]
    public void Transform_gizmo_mode_change_rebuilds_state_and_advances_presentation_fence()
    {
        var document = Document(revision: 1, entityCount: 1);
        var selectedObjectId = document.Entities.Single().ObjectId;
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);
        session.SetSelection(viewStateRevision: 1, selectedObjectId);
        Assert.True(session.TryPublishLatest(size, out var translate));

        session.SetTransformGizmoKind(ViewportTransformGizmoKind.Rotate);

        Assert.True(session.Current.MinimumPresentableSequence > translate.Sequence);
        Assert.False(session.CanPresentPublishedFrame(
            translate.Sequence,
            translate.TargetRevision));
        Assert.True(session.TryPublishLatest(size, out var rotate));
        Assert.Equal(ViewportTransformGizmoKind.Rotate, rotate.TransformGizmo!.Kind);
        Assert.True(rotate.Reasons.HasFlag(ViewportInvalidationReason.GizmoModeChanged));
    }

    [Fact]
    public void Refresh_request_is_coalesced_until_the_latest_state_is_published()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        var size = new ViewportRenderSize(extent, extent);
        Assert.True(session.TryPublishLatest(size, out _));
        var refreshRequests = 0;
        session.RefreshRequested += (_, _) =>
        {
            _ = session.Current;
            refreshRequests++;
        };

        session.SetCamera(CameraAt(2));
        var finalCamera = CameraAt(4);
        session.SetCamera(finalCamera);

        Assert.Equal(1, refreshRequests);
        Assert.True(session.TryPublishLatest(size, out var request));
        Assert.Equal(finalCamera, request.Camera);

        session.Invalidate(ViewportInvalidationReason.Exposed);
        Assert.Equal(2, refreshRequests);
    }

    [Fact]
    public void Failed_current_render_requeues_its_invalidation_without_continuous_scheduling()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var extent = new ViewportExtent(640, 360);
        Assert.True(session.TryBeginRender(extent, out var failed));
        var refreshRequests = 0;
        session.RefreshRequested += (_, _) => refreshRequests++;

        Assert.False(session.CompleteRender(
            failed.Sequence,
            failed.TargetRevision,
            succeeded: false));
        Assert.Equal(1, refreshRequests);
        Assert.True(session.TryBeginRender(extent, out var retry));
        Assert.Equal(failed.Reasons, retry.Reasons);
        Assert.True(session.CompleteRender(retry.Sequence, retry.TargetRevision, succeeded: true));
        Assert.False(session.TryBeginRender(extent, out _));
        Assert.Equal(1, refreshRequests);
    }

    [Fact]
    public void Closed_session_rejects_new_work_and_foreign_documents()
    {
        var document = Document(revision: 1, entityCount: 0);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);

        Assert.Throws<ArgumentException>(() => session.SynchronizeDocument(
            Document(revision: 2, entityCount: 0)));
        session.Close();

        Assert.True(session.Current.IsClosed);
        Assert.False(session.TryBeginRender(new ViewportExtent(1, 1), out _));
    }

    [Fact]
    public void V7_publish_does_not_wait_for_an_older_frame_completion()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var size = new ViewportRenderSize(
            new ViewportExtent(640, 360),
            new ViewportExtent(640, 368));

        Assert.True(session.TryPublishLatest(size, out var first));
        session.Invalidate(ViewportInvalidationReason.Exposed);
        Assert.True(session.TryPublishLatest(size, out var second));

        Assert.Equal(first.Sequence + 1U, second.Sequence);
        Assert.False(session.Current.IsFrameInFlight);
        Assert.True(session.MarkPublishedFramePresented(second.Sequence, second.TargetRevision));
    }

    [Fact]
    public void V7_rejected_publish_restores_its_invalidation_reasons()
    {
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            Document(revision: 1, entityCount: 0),
            ViewportCameraSnapshot.DefaultScene);
        var size = new ViewportRenderSize(
            new ViewportExtent(640, 360),
            new ViewportExtent(640, 368));

        Assert.True(session.TryPublishLatest(size, out var rejected));
        session.RetryPublishedFrame(rejected);
        Assert.True(session.TryPublishLatest(size, out var retry));

        Assert.Equal(rejected.Reasons, retry.Reasons);
        Assert.Equal(rejected.Sequence + 1U, retry.Sequence);
    }

    [Fact]
    public void Camera_and_document_inputs_fail_before_the_native_boundary()
    {
        var document = Document(revision: 3, entityCount: 0);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        var stale = new SceneDocumentSnapshot(
            document.SceneId,
            document.Path,
            revision: 2,
            savedRevision: 1,
            document.Entities);

        Assert.Throws<ArgumentOutOfRangeException>(() => session.SynchronizeDocument(stale));
        Assert.Throws<ArgumentException>(() => new ViewportCameraSnapshot(
            Float3.Zero,
            Float3.Zero,
            new Float3(0, 1, 0),
            MathF.PI / 3,
            ViewportFieldOfViewAxis.MaintainHorizontal,
            0.1f,
            1000.0f));
        Assert.Throws<ArgumentException>(() => new ViewportCameraSnapshot(
            new Float3(0, 2, -6),
            Float3.Zero,
            new Float3(0, 1, 0),
            0,
            ViewportFieldOfViewAxis.MaintainHorizontal,
            0.1f,
            1000.0f));
        Assert.Throws<ArgumentException>(() => new ViewportCameraSnapshot(
            new Float3(0, 2, -6),
            Float3.Zero,
            new Float3(0, 1, 0),
            MathF.PI / 3,
            (ViewportFieldOfViewAxis)42,
            0.1f,
            1000.0f));
    }

    [Fact]
    public void Oversized_authored_mesh_snapshot_does_not_partially_advance_the_session()
    {
        var sceneId = Guid.NewGuid();
        var originalObjectId = Guid.NewGuid();
        var original = new SceneDocumentSnapshot(
            sceneId,
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision: 1,
            savedRevision: 1,
            [
                new SceneEntitySnapshot(
                    originalObjectId,
                    new EntityId(1, 1),
                    "Original mesh",
                    TransformValue.Identity,
                    SceneMeshReference.DirectionalWedgeValidation),
            ]);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            original,
            ViewportCameraSnapshot.DefaultScene);
        var size = new ViewportRenderSize(
            new ViewportExtent(640, 360),
            new ViewportExtent(640, 360));
        Assert.True(session.TryPublishLatest(size, out var first));
        Assert.Equal(originalObjectId, Assert.Single(first.AuthoredMeshes).ObjectId);

        var oversized = new SceneDocumentSnapshot(
            sceneId,
            original.Path,
            revision: 2,
            savedRevision: 1,
            Enumerable.Range(0, ViewportRenderRequest.MaximumAuthoredMeshCount + 1)
                .Select(index => new SceneEntitySnapshot(
                    Guid.NewGuid(),
                    new EntityId(checked((uint)index) + 1U, 1U),
                    $"Mesh {index}",
                    TransformValue.Identity,
                    SceneMeshReference.DirectionalWedgeValidation)));

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            session.SynchronizeDocument(oversized));

        Assert.Equal(original.Revision, session.Current.TargetRevision);
        session.Invalidate(ViewportInvalidationReason.TargetChanged);
        Assert.True(session.TryPublishLatest(size, out var retained));
        Assert.Equal(original.Revision, retained.TargetRevision);
        Assert.Equal(originalObjectId, Assert.Single(retained.AuthoredMeshes).ObjectId);
    }

    private static SceneDocumentSnapshot Document(ulong revision, int entityCount)
    {
        var sceneId = Guid.NewGuid();
        var entities = Enumerable.Range(0, entityCount)
            .Select(index => new SceneEntitySnapshot(
                Guid.NewGuid(),
                new EntityId(checked((uint)index + 1U), 1U),
                $"Entity {index}",
                new TransformValue(
                    new Float3(index, 0, 0),
                    Quaternion.Identity,
                    Float3.One)));
        return new SceneDocumentSnapshot(
            sceneId,
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision,
            savedRevision: 1,
            entities);
    }

    private static ViewportCameraSnapshot CameraAt(float x) => new(
        new Float3(x, 3, -7),
        Float3.Zero,
        new Float3(0, 1, 0),
        MathF.PI / 2,
        ViewportFieldOfViewAxis.MaintainHorizontal,
        0.1f,
        1000.0f);
}
