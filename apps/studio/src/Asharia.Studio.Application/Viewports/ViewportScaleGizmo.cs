using System;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Viewports;

public sealed class ViewportScaleGizmoInteraction : IViewportTransformGizmoInteraction
{
    private const float MinimumPickSegmentPosition = 0.12f;
    private const float MinimumScaleFactor = 0.01f;
    private const float MaximumScaleFactor = 100.0f;
    private readonly ViewportPickPoint initialPointer_;
    private readonly ViewportGizmoMath.ProjectedPoint screenAxis_;

    private ViewportScaleGizmoInteraction(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportExtent extent,
        ViewportGizmoAxis axis,
        ViewportPickPoint initialPointer,
        ViewportGizmoMath.ProjectedPoint screenAxis)
    {
        ObjectId = snapshot.ObjectId;
        ExpectedRevision = snapshot.TargetRevision;
        Extent = extent;
        Axis = axis;
        InitialTransform = snapshot.Transform;
        CurrentTransform = snapshot.Transform;
        initialPointer_ = initialPointer;
        screenAxis_ = screenAxis;
    }

    public Guid ObjectId { get; }

    public ulong ExpectedRevision { get; }

    public ViewportExtent Extent { get; }

    public ViewportGizmoAxis Axis { get; }

    public TransformValue InitialTransform { get; }

    public TransformValue CurrentTransform { get; private set; }

    public bool HasChanged =>
        ViewportGizmoMath.DistanceSquared(InitialTransform.Scale, CurrentTransform.Scale) >
        1.0e-10f;

    public bool TryUpdate(ViewportPickPoint point, out TransformValue transform)
    {
        transform = CurrentTransform;
        var pointerX = point.X - initialPointer_.X;
        var pointerY = point.Y - initialPointer_.Y;
        var factor = Math.Clamp(
            1.0f +
            (pointerX * screenAxis_.X + pointerY * screenAxis_.Y) /
            ViewportGizmoMath.LengthPixels,
            MinimumScaleFactor,
            MaximumScaleFactor);
        if (!float.IsFinite(factor))
        {
            return false;
        }

        var scale = SetAxisComponent(
            InitialTransform.Scale,
            Axis,
            AxisComponent(InitialTransform.Scale, Axis) * factor);
        if (!ViewportGizmoMath.IsFinite(scale))
        {
            return false;
        }

        CurrentTransform = new TransformValue(
            InitialTransform.Position,
            InitialTransform.Rotation,
            scale);
        transform = CurrentTransform;
        return true;
    }

    internal static bool TryCreate(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out ViewportScaleGizmoInteraction interaction)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        interaction = null!;
        if (snapshot.Kind != ViewportTransformGizmoKind.Scale ||
            !ViewportGizmoMath.TryCreateProjection(
                snapshot.Camera,
                request.Extent,
                out var projection) ||
            !TryHitTest(snapshot, projection, request, out var axis) ||
            !TryScreenAxis(snapshot, projection, axis, out var screenAxis))
        {
            return false;
        }

        interaction = new ViewportScaleGizmoInteraction(
            snapshot,
            request.Extent,
            axis,
            request.Point,
            screenAxis);
        return true;
    }

    internal static ViewportGizmoAxis HitTest(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        return snapshot.Kind == ViewportTransformGizmoKind.Scale &&
            ViewportGizmoMath.TryCreateProjection(
                snapshot.Camera,
                request.Extent,
                out var projection) &&
            TryHitTest(snapshot, projection, request, out var axis)
                ? axis
                : ViewportGizmoAxis.None;
    }

    private static bool TryHitTest(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportGizmoMath.Projection projection,
        ViewportPickRequest request,
        out ViewportGizmoAxis axis)
    {
        axis = ViewportGizmoAxis.None;
        var origin = snapshot.Transform.Position;
        if (request.Point.X < 0 || request.Point.Y < 0 ||
            request.Point.X >= request.Extent.Width ||
            request.Point.Y >= request.Extent.Height ||
            !ViewportGizmoMath.TryWorldLength(projection, origin, out var worldLength) ||
            !ViewportGizmoMath.TryProject(projection, origin, out var start))
        {
            return false;
        }

        var bestDistance = float.PositiveInfinity;
        foreach (var candidateAxis in new[]
                 {
                     ViewportGizmoAxis.X,
                     ViewportGizmoAxis.Y,
                     ViewportGizmoAxis.Z,
                 })
        {
            if (MathF.Abs(AxisComponent(snapshot.Transform.Scale, candidateAxis)) <=
                ViewportGizmoMath.Epsilon)
            {
                continue;
            }

            var localAxis = ViewportGizmoMath.Rotate(
                snapshot.Transform.Rotation,
                ViewportGizmoMath.AxisDirection(candidateAxis));
            var endpoint = ViewportGizmoMath.Add(
                origin,
                ViewportGizmoMath.Scale(localAxis, worldLength));
            if (!ViewportGizmoMath.TryProject(projection, endpoint, out var end))
            {
                continue;
            }

            var segmentX = end.X - start.X;
            var segmentY = end.Y - start.Y;
            if (segmentX * segmentX + segmentY * segmentY <= ViewportGizmoMath.Epsilon)
            {
                continue;
            }

            var distance = ViewportGizmoMath.DistanceToSegment(
                request.Point,
                start,
                end,
                out var segmentPosition);
            if (segmentPosition < MinimumPickSegmentPosition ||
                distance > request.TolerancePixels || distance >= bestDistance)
            {
                continue;
            }

            bestDistance = distance;
            axis = candidateAxis;
        }

        return axis != ViewportGizmoAxis.None;
    }

    private static bool TryScreenAxis(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportGizmoMath.Projection projection,
        ViewportGizmoAxis axis,
        out ViewportGizmoMath.ProjectedPoint screenAxis)
    {
        screenAxis = default;
        var origin = snapshot.Transform.Position;
        if (!ViewportGizmoMath.TryWorldLength(projection, origin, out var worldLength) ||
            !ViewportGizmoMath.TryProject(projection, origin, out var start))
        {
            return false;
        }

        var localAxis = ViewportGizmoMath.Rotate(
            snapshot.Transform.Rotation,
            ViewportGizmoMath.AxisDirection(axis));
        var endpoint = ViewportGizmoMath.Add(
            origin,
            ViewportGizmoMath.Scale(localAxis, worldLength));
        if (!ViewportGizmoMath.TryProject(projection, endpoint, out var end))
        {
            return false;
        }

        var x = end.X - start.X;
        var y = end.Y - start.Y;
        var length = MathF.Sqrt(x * x + y * y);
        if (!float.IsFinite(length) || length <= ViewportGizmoMath.Epsilon)
        {
            return false;
        }

        screenAxis = new ViewportGizmoMath.ProjectedPoint(x / length, y / length);
        return true;
    }

    private static float AxisComponent(Float3 value, ViewportGizmoAxis axis) => axis switch
    {
        ViewportGizmoAxis.X => value.X,
        ViewportGizmoAxis.Y => value.Y,
        ViewportGizmoAxis.Z => value.Z,
        _ => throw new ArgumentOutOfRangeException(nameof(axis), axis, null),
    };

    private static Float3 SetAxisComponent(
        Float3 value,
        ViewportGizmoAxis axis,
        float component) => axis switch
        {
            ViewportGizmoAxis.X => new Float3(component, value.Y, value.Z),
            ViewportGizmoAxis.Y => new Float3(value.X, component, value.Z),
            ViewportGizmoAxis.Z => new Float3(value.X, value.Y, component),
            _ => throw new ArgumentOutOfRangeException(nameof(axis), axis, null),
        };
}

public static class ViewportScaleGizmoManipulator
{
    public static ViewportGizmoAxis HitTest(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request) =>
        ViewportScaleGizmoInteraction.HitTest(snapshot, request);

    public static bool TryBegin(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out ViewportScaleGizmoInteraction interaction) =>
        ViewportScaleGizmoInteraction.TryCreate(snapshot, request, out interaction);
}
