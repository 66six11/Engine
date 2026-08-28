using System;
using System.Linq;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Viewports;
using Xunit;

namespace Asharia.Studio.Application.Tests.Viewports;

public sealed class ViewportTransformProxyPickerTests
{
    [Fact]
    public void Center_click_hits_the_visible_identity_transform_proxy()
    {
        var objectId = Guid.NewGuid();
        var snapshot = Capture(
            ViewportCameraSnapshot.DefaultScene,
            Proxy(objectId, Float3.Zero));

        var result = ViewportTransformProxyPicker.Pick(
            snapshot,
            Request(800, 600, 400, 300));

        Assert.True(result.IsHit);
        Assert.Equal(objectId, result.ObjectId);
        Assert.InRange(result.ScreenDistancePixels, 0, 0.001f);
    }

    [Fact]
    public void Empty_screen_space_returns_a_miss()
    {
        var snapshot = Capture(
            ViewportCameraSnapshot.DefaultScene,
            Proxy(Guid.NewGuid(), Float3.Zero));

        var result = ViewportTransformProxyPicker.Pick(
            snapshot,
            Request(800, 600, 32, 32));

        Assert.False(result.IsHit);
        Assert.Null(result.ObjectId);
    }

    [Fact]
    public void Screen_degenerate_zero_scale_proxy_is_not_pickable()
    {
        var objectId = Guid.NewGuid();
        var snapshot = Capture(
            ViewportCameraSnapshot.DefaultScene,
            Proxy(objectId, Float3.Zero, Float3.Zero));

        var result = ViewportTransformProxyPicker.Pick(
            snapshot,
            Request(800, 600, 400, 300));

        Assert.False(result.IsHit);
    }

    [Fact]
    public void Nearest_overlapping_proxy_wins_before_screen_distance()
    {
        var nearId = Guid.NewGuid();
        var farId = Guid.NewGuid();
        var camera = new ViewportCameraSnapshot(
            Float3.Zero,
            new Float3(0, 0, 1),
            new Float3(0, 1, 0),
            MathF.PI / 2,
            ViewportFieldOfViewAxis.MaintainHorizontal,
            0.1f,
            1000.0f);
        var snapshot = Capture(
            camera,
            Proxy(farId, new Float3(0, 0, 6)),
            Proxy(nearId, new Float3(0, 0, 3)));

        var result = ViewportTransformProxyPicker.Pick(
            snapshot,
            Request(640, 480, 320, 240));

        Assert.Equal(nearId, result.ObjectId);
        Assert.Equal(3.0f, result.CameraDepth);
    }

    [Fact]
    public void Stable_object_identity_breaks_an_exact_overlap_tie()
    {
        var firstId = Guid.Parse("00000000-0000-0000-0000-000000000001");
        var secondId = Guid.Parse("00000000-0000-0000-0000-000000000002");
        var snapshot = Capture(
            ViewportCameraSnapshot.DefaultScene,
            Proxy(secondId, Float3.Zero),
            Proxy(firstId, Float3.Zero));

        var result = ViewportTransformProxyPicker.Pick(
            snapshot,
            Request(800, 600, 400, 300));

        Assert.Equal(firstId, result.ObjectId);
    }

    [Fact]
    public void Proxy_behind_the_camera_is_not_pickable()
    {
        var camera = new ViewportCameraSnapshot(
            Float3.Zero,
            new Float3(0, 0, 1),
            new Float3(0, 1, 0),
            MathF.PI / 2,
            ViewportFieldOfViewAxis.MaintainHorizontal,
            0.1f,
            1000.0f);
        var snapshot = Capture(
            camera,
            Proxy(Guid.NewGuid(), new Float3(0, 0, -2)));

        var result = ViewportTransformProxyPicker.Pick(
            snapshot,
            Request(640, 480, 320, 240));

        Assert.False(result.IsHit);
    }

    [Theory]
    [InlineData(1.0f)]
    [InlineData(4.0f)]
    public void Projection_matches_debug_overlay_near_and_far_plane_behavior(float depth)
    {
        var objectId = Guid.NewGuid();
        var camera = new ViewportCameraSnapshot(
            Float3.Zero,
            new Float3(0, 0, 1),
            new Float3(0, 1, 0),
            MathF.PI / 2,
            ViewportFieldOfViewAxis.MaintainHorizontal,
            nearPlane: 2.0f,
            farPlane: 3.0f);
        var snapshot = Capture(camera, Proxy(objectId, new Float3(0, 0, depth)));

        var result = ViewportTransformProxyPicker.Pick(
            snapshot,
            Request(640, 480, 320, 240));

        Assert.Equal(objectId, result.ObjectId);
    }

    [Theory]
    [InlineData(ViewportFieldOfViewAxis.MaintainHorizontal)]
    [InlineData(ViewportFieldOfViewAxis.MaintainVertical)]
    public void Projection_respects_the_declared_field_of_view_axis(
        ViewportFieldOfViewAxis axis)
    {
        var objectId = Guid.NewGuid();
        var camera = new ViewportCameraSnapshot(
            Float3.Zero,
            new Float3(0, 0, 1),
            new Float3(0, 1, 0),
            MathF.PI / 2,
            axis,
            0.1f,
            1000.0f);
        var snapshot = Capture(
            camera,
            Proxy(objectId, new Float3(0, 0, 4)));

        var result = ViewportTransformProxyPicker.Pick(
            snapshot,
            Request(800, 400, 400, 200));

        Assert.Equal(objectId, result.ObjectId);
    }

    [Fact]
    public void Session_snapshot_rejects_stale_or_closed_identity()
    {
        var document = Document(3, Proxy(Guid.NewGuid(), Float3.Zero));
        var sessionId = ViewportSessionId.Create();
        var session = new ViewportSession(
            sessionId,
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);

        Assert.False(session.TryCapturePickSnapshot(
            ViewportSessionId.Create(),
            document.SceneId,
            document.Revision,
            out _));
        Assert.False(session.TryCapturePickSnapshot(
            sessionId,
            document.SceneId,
            document.Revision - 1,
            out _));
        Assert.True(session.TryCapturePickSnapshot(
            sessionId,
            document.SceneId,
            document.Revision,
            out var current));
        Assert.Equal(document.Revision, current.TargetRevision);

        session.Close();

        Assert.False(session.TryCapturePickSnapshot(
            sessionId,
            document.SceneId,
            document.Revision,
            out _));
    }

    [Fact]
    public void Snapshot_preserves_the_visible_proxy_bound_and_truncation_evidence()
    {
        var entities = Enumerable.Range(0, ViewportRenderRequest.MaximumDebugProxyCount + 1)
            .Select(index => Proxy(Guid.NewGuid(), new Float3(index, 0, 0)))
            .ToArray();
        var document = Document(1, entities);
        var sessionId = ViewportSessionId.Create();
        var session = new ViewportSession(
            sessionId,
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);

        Assert.True(session.TryCapturePickSnapshot(
            sessionId,
            document.SceneId,
            document.Revision,
            out var snapshot));

        Assert.Equal(ViewportRenderRequest.MaximumDebugProxyCount, snapshot.DebugProxies.Count);
        Assert.Equal(entities.Length, snapshot.TotalDebugProxyCount);
        Assert.True(snapshot.DebugProxiesTruncated);
    }

    private static ViewportPickRequest Request(
        uint width,
        uint height,
        float x,
        float y) =>
        new(new ViewportExtent(width, height), new ViewportPickPoint(x, y), 6.0f);

    private static ViewportPickSnapshot Capture(
        ViewportCameraSnapshot camera,
        params SceneEntitySnapshot[] entities)
    {
        var document = Document(1, entities);
        var sessionId = ViewportSessionId.Create();
        var session = new ViewportSession(
            sessionId,
            ViewportRenderKind.Scene,
            document,
            camera);
        return session.TryCapturePickSnapshot(
            sessionId,
            document.SceneId,
            document.Revision,
            out var snapshot)
                ? snapshot
                : throw new InvalidOperationException("Pick snapshot was not captured.");
    }

    private static SceneDocumentSnapshot Document(
        ulong revision,
        params SceneEntitySnapshot[] entities) =>
        new(
            Guid.NewGuid(),
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision,
            savedRevision: revision,
            entities);

    private static SceneEntitySnapshot Proxy(Guid objectId, Float3 position) =>
        Proxy(objectId, position, Float3.One);

    private static SceneEntitySnapshot Proxy(
        Guid objectId,
        Float3 position,
        Float3 scale) =>
        new(
            objectId,
            new EntityId(BitConverter.ToUInt32(objectId.ToByteArray(), 0) | 1U, 1U),
            "Proxy",
            new TransformValue(position, Quaternion.Identity, scale));
}
