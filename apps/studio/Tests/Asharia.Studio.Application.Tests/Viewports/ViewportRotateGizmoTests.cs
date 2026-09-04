using System;
using Asharia.Runtime;
using Asharia.Studio.Application.Viewports;
using Xunit;

namespace Asharia.Studio.Application.Tests.Viewports;

public sealed class ViewportRotateGizmoTests
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
    public void Hit_test_selects_world_ring_away_from_overlapping_cardinals()
    {
        Assert.Equal(
            ViewportGizmoAxis.Z,
            ViewportRotateGizmoManipulator.HitTest(
                Snapshot(),
                Request(459.4f, 240.6f)));
        Assert.Equal(
            ViewportGizmoAxis.None,
            ViewportRotateGizmoManipulator.HitTest(
                Snapshot(),
                Request(400, 300)));
    }

    [Fact]
    public void Face_on_drag_accumulates_a_normalized_world_axis_rotation()
    {
        Assert.True(ViewportRotateGizmoManipulator.TryBegin(
            Snapshot(),
            Request(459.4f, 240.6f),
            out var interaction));

        Assert.Equal(ViewportGizmoAxis.Z, interaction.Axis);
        Assert.True(interaction.TryUpdate(new ViewportPickPoint(459.4f, 359.4f), out var transform));
        Assert.Equal(Float3.Zero, transform.Position);
        Assert.Equal(Float3.One, transform.Scale);
        Assert.InRange(MathF.Abs(transform.Rotation.Z), 0.70f, 0.72f);
        Assert.InRange(MathF.Abs(transform.Rotation.W), 0.70f, 0.72f);
        Assert.InRange(LengthSquared(transform.Rotation), 0.9999f, 1.0001f);
        Assert.True(interaction.HasChanged);
    }

    [Fact]
    public void Edge_on_ring_uses_stable_screen_tangent_fallback()
    {
        Assert.True(ViewportRotateGizmoManipulator.TryBegin(
            Snapshot(),
            Request(400, 240),
            out var interaction));

        Assert.Equal(ViewportGizmoAxis.X, interaction.Axis);
        Assert.True(interaction.TryUpdate(new ViewportPickPoint(400, 280), out var transform));
        Assert.True(MathF.Abs(transform.Rotation.X) > 0.1f);
        Assert.Equal(0, transform.Rotation.Y);
        Assert.Equal(0, transform.Rotation.Z);
        Assert.InRange(LengthSquared(transform.Rotation), 0.9999f, 1.0001f);
    }

    [Fact]
    public void Incremental_angles_cross_the_wrap_boundary_and_full_turn_is_a_noop()
    {
        Assert.True(ViewportRotateGizmoManipulator.TryBegin(
            Snapshot(),
            Request(459.4f, 240.6f),
            out var interaction));

        Assert.True(interaction.TryUpdate(new ViewportPickPoint(340.6f, 240.6f), out _));
        Assert.True(interaction.TryUpdate(new ViewportPickPoint(340.6f, 359.4f), out _));
        Assert.True(interaction.TryUpdate(new ViewportPickPoint(459.4f, 359.4f), out _));
        Assert.True(interaction.TryUpdate(new ViewportPickPoint(459.4f, 240.6f), out var transform));

        Assert.InRange(MathF.Abs(transform.Rotation.W), 0.9999f, 1.0001f);
        Assert.False(interaction.HasChanged);
    }

    [Fact]
    public void Noop_tolerates_a_nearly_normalized_source_rotation()
    {
        var snapshot = new ViewportTransformGizmoSnapshot(
            ViewportTransformGizmoKind.Rotate,
            Guid.NewGuid(),
            targetRevision: 7,
            Camera,
            new TransformValue(
                Float3.Zero,
                new Quaternion(0, 0, 0, 0.99975f),
                Float3.One));

        Assert.True(ViewportRotateGizmoManipulator.TryBegin(
            snapshot,
            Request(459.4f, 240.6f),
            out var interaction));

        Assert.False(interaction.HasChanged);
    }

    [Fact]
    public void Manipulators_reject_a_snapshot_for_the_other_mode()
    {
        var rotate = Snapshot();
        var translate = new ViewportTransformGizmoSnapshot(
            ViewportTransformGizmoKind.Translate,
            rotate.ObjectId,
            rotate.TargetRevision,
            rotate.Camera,
            rotate.Transform);

        Assert.False(ViewportTranslateGizmoManipulator.TryBegin(
            rotate,
            Request(460, 300),
            out _));
        Assert.False(ViewportRotateGizmoManipulator.TryBegin(
            translate,
            Request(459.4f, 240.6f),
            out _));
    }

    private static ViewportTransformGizmoSnapshot Snapshot() => new(
        ViewportTransformGizmoKind.Rotate,
        Guid.NewGuid(),
        targetRevision: 7,
        Camera,
        TransformValue.Identity);

    private static ViewportPickRequest Request(float x, float y) => new(
        Extent,
        new ViewportPickPoint(x, y),
        tolerancePixels: 8);

    private static float LengthSquared(Quaternion value) =>
        value.X * value.X + value.Y * value.Y +
        value.Z * value.Z + value.W * value.W;
}
