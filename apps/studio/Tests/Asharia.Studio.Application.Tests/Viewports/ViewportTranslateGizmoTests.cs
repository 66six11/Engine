using System;
using Asharia.Runtime;
using Asharia.Studio.Application.Viewports;
using Xunit;

namespace Asharia.Studio.Application.Tests.Viewports;

public sealed class ViewportTranslateGizmoTests
{
    private static readonly ViewportExtent Extent = new(800, 600);

    private static readonly ViewportCameraSnapshot Camera = new(
        new Float3(0, 0, -10),
        Float3.Zero,
        new Float3(0, 1, 0),
        MathF.PI / 2,
        ViewportFieldOfViewAxis.MaintainHorizontal,
        0.1f,
        1000.0f);

    [Fact]
    public void Hit_test_selects_visible_world_axis_and_leaves_center_ambiguous()
    {
        var snapshot = Snapshot();

        Assert.Equal(
            ViewportGizmoAxis.X,
            ViewportTranslateGizmoManipulator.HitTest(
                snapshot,
                Request(460, 300)));
        Assert.Equal(
            ViewportGizmoAxis.Y,
            ViewportTranslateGizmoManipulator.HitTest(
                snapshot,
                Request(400, 240)));
        Assert.Equal(
            ViewportGizmoAxis.None,
            ViewportTranslateGizmoManipulator.HitTest(
                snapshot,
                Request(400, 300)));
    }

    [Fact]
    public void Drag_projects_pointer_motion_onto_one_world_axis()
    {
        Assert.True(ViewportTranslateGizmoManipulator.TryBegin(
            Snapshot(),
            Request(460, 300),
            out var interaction));

        Assert.Equal(ViewportGizmoAxis.X, interaction.Axis);
        Assert.True(interaction.TryUpdate(new ViewportPickPoint(500, 300), out var transform));
        Assert.InRange(transform.Position.X, 0.9999f, 1.0001f);
        Assert.Equal(0, transform.Position.Y);
        Assert.Equal(0, transform.Position.Z);
        Assert.Equal(Quaternion.Identity, transform.Rotation);
        Assert.Equal(Float3.One, transform.Scale);
        Assert.True(interaction.HasChanged);
    }

    [Fact]
    public void Camera_parallel_axis_is_not_falsely_pickable()
    {
        var snapshot = Snapshot();

        Assert.Equal(
            ViewportGizmoAxis.None,
            ViewportTranslateGizmoManipulator.HitTest(
                snapshot,
                Request(400, 360)));
        Assert.False(ViewportTranslateGizmoManipulator.TryBegin(
            snapshot,
            Request(400, 360),
            out _));
    }

    private static ViewportTranslateGizmoSnapshot Snapshot() => new(
        Guid.NewGuid(),
        targetRevision: 7,
        Camera,
        TransformValue.Identity);

    private static ViewportPickRequest Request(float x, float y) => new(
        Extent,
        new ViewportPickPoint(x, y),
        tolerancePixels: 8);
}
