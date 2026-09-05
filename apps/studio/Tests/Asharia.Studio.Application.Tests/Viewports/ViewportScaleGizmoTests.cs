using System;
using Asharia.Runtime;
using Asharia.Studio.Application.Viewports;
using Xunit;

namespace Asharia.Studio.Application.Tests.Viewports;

public sealed class ViewportScaleGizmoTests
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
    public void Hit_test_uses_rotated_local_axes_and_leaves_center_ambiguous()
    {
        var quarterTurnAroundZ = new Quaternion(
            0,
            0,
            MathF.Sqrt(0.5f),
            MathF.Sqrt(0.5f));
        var snapshot = Snapshot(new TransformValue(
            Float3.Zero,
            quarterTurnAroundZ,
            Float3.One));

        Assert.Equal(
            ViewportGizmoAxis.X,
            ViewportScaleGizmoManipulator.HitTest(snapshot, Request(400, 240)));
        Assert.Equal(
            ViewportGizmoAxis.Y,
            ViewportScaleGizmoManipulator.HitTest(snapshot, Request(340, 300)));
        Assert.Equal(
            ViewportGizmoAxis.None,
            ViewportScaleGizmoManipulator.HitTest(snapshot, Request(400, 300)));
    }

    [Fact]
    public void Drag_changes_only_one_scale_component_from_the_fixed_start()
    {
        var initial = new TransformValue(
            Float3.Zero,
            Quaternion.Identity,
            new Float3(2, 3, 4));
        Assert.True(ViewportScaleGizmoManipulator.TryBegin(
            Snapshot(initial),
            Request(460, 300),
            out var interaction));

        Assert.Equal(ViewportGizmoAxis.X, interaction.Axis);
        Assert.True(interaction.TryUpdate(new ViewportPickPoint(502, 300), out var transform));
        Assert.InRange(transform.Scale.X, 2.9999f, 3.0001f);
        Assert.Equal(3, transform.Scale.Y);
        Assert.Equal(4, transform.Scale.Z);
        Assert.Equal(initial.Position, transform.Position);
        Assert.Equal(initial.Rotation, transform.Rotation);
        Assert.True(interaction.HasChanged);

        Assert.True(interaction.TryUpdate(new ViewportPickPoint(460, 300), out transform));
        Assert.Equal(initial, transform);
        Assert.False(interaction.HasChanged);
    }

    [Fact]
    public void Drag_preserves_mirror_sign_and_cannot_cross_zero()
    {
        var initial = new TransformValue(
            Float3.Zero,
            Quaternion.Identity,
            new Float3(-2, 3, 4));
        Assert.True(ViewportScaleGizmoManipulator.TryBegin(
            Snapshot(initial),
            Request(460, 300),
            out var interaction));

        Assert.True(interaction.TryUpdate(new ViewportPickPoint(0, 300), out var transform));
        Assert.InRange(transform.Scale.X, -0.02001f, -0.01999f);
        Assert.Equal(3, transform.Scale.Y);
        Assert.Equal(4, transform.Scale.Z);
    }

    [Fact]
    public void Zero_scale_and_camera_parallel_axes_fail_closed()
    {
        var zeroX = Snapshot(new TransformValue(
            Float3.Zero,
            Quaternion.Identity,
            new Float3(0, 1, 1)));

        Assert.Equal(
            ViewportGizmoAxis.None,
            ViewportScaleGizmoManipulator.HitTest(zeroX, Request(460, 300)));
        Assert.False(ViewportScaleGizmoManipulator.TryBegin(
            zeroX,
            Request(460, 300),
            out _));
        Assert.Equal(
            ViewportGizmoAxis.None,
            ViewportScaleGizmoManipulator.HitTest(
                Snapshot(TransformValue.Identity),
                Request(400, 360)));
    }

    [Fact]
    public void Manipulator_rejects_a_snapshot_for_another_mode()
    {
        var snapshot = new ViewportTransformGizmoSnapshot(
            ViewportTransformGizmoKind.Translate,
            Guid.NewGuid(),
            targetRevision: 7,
            Camera,
            TransformValue.Identity);

        Assert.False(ViewportScaleGizmoManipulator.TryBegin(
            snapshot,
            Request(460, 300),
            out _));
    }

    private static ViewportTransformGizmoSnapshot Snapshot(TransformValue transform) => new(
        ViewportTransformGizmoKind.Scale,
        Guid.NewGuid(),
        targetRevision: 7,
        Camera,
        transform);

    private static ViewportPickRequest Request(float x, float y) => new(
        Extent,
        new ViewportPickPoint(x, y),
        tolerancePixels: 8);
}
