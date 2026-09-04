using System;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Viewports;

public sealed class ViewportRotateGizmoInteraction : IViewportTransformGizmoInteraction
{
    private const int RingSegmentCount = 64;
    private const float MinimumPlaneAlignment = 0.15f;
    private const float MaximumAccumulatedAngle = 32.0f * MathF.PI;
    private readonly ViewportGizmoMath.Projection projection_;
    private readonly Float3 axisDirection_;
    private readonly RotationDragStrategy strategy_;
    private readonly ViewportPickPoint initialPointer_;
    private readonly ViewportGizmoMath.ProjectedPoint screenTangent_;
    private Float3 previousRadial_;
    private float accumulatedAngle_;

    private ViewportRotateGizmoInteraction(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportExtent extent,
        ViewportGizmoAxis axis,
        ViewportGizmoMath.Projection projection,
        Float3 axisDirection,
        RotationDragStrategy strategy,
        ViewportPickPoint initialPointer,
        ViewportGizmoMath.ProjectedPoint screenTangent,
        Float3 previousRadial)
    {
        ObjectId = snapshot.ObjectId;
        ExpectedRevision = snapshot.TargetRevision;
        Extent = extent;
        Axis = axis;
        InitialTransform = snapshot.Transform;
        CurrentTransform = snapshot.Transform;
        projection_ = projection;
        axisDirection_ = axisDirection;
        strategy_ = strategy;
        initialPointer_ = initialPointer;
        screenTangent_ = screenTangent;
        previousRadial_ = previousRadial;
    }

    public Guid ObjectId { get; }

    public ulong ExpectedRevision { get; }

    public ViewportExtent Extent { get; }

    public ViewportGizmoAxis Axis { get; }

    public TransformValue InitialTransform { get; }

    public TransformValue CurrentTransform { get; private set; }

    public bool HasChanged
    {
        get
        {
            var orientationDot =
                InitialTransform.Rotation.X * CurrentTransform.Rotation.X +
                InitialTransform.Rotation.Y * CurrentTransform.Rotation.Y +
                InitialTransform.Rotation.Z * CurrentTransform.Rotation.Z +
                InitialTransform.Rotation.W * CurrentTransform.Rotation.W;
            var initialLengthSquared = LengthSquared(InitialTransform.Rotation);
            var currentLengthSquared = LengthSquared(CurrentTransform.Rotation);
            var normalizedDot = MathF.Abs(orientationDot) /
                MathF.Sqrt(initialLengthSquared * currentLengthSquared);
            return 1.0f - Math.Clamp(normalizedDot, 0.0f, 1.0f) > 1.0e-6f;
        }
    }

    public bool TryUpdate(ViewportPickPoint point, out TransformValue transform)
    {
        transform = CurrentTransform;
        float angle;
        if (strategy_ == RotationDragStrategy.RayPlane)
        {
            if (!TryRadial(point, out var radial))
            {
                return false;
            }

            var sine = ViewportGizmoMath.Dot(
                axisDirection_,
                ViewportGizmoMath.Cross(previousRadial_, radial));
            var cosine = Math.Clamp(
                ViewportGizmoMath.Dot(previousRadial_, radial),
                -1.0f,
                1.0f);
            var incrementalAngle = MathF.Atan2(sine, cosine);
            if (!float.IsFinite(incrementalAngle))
            {
                return false;
            }

            accumulatedAngle_ = Math.Clamp(
                accumulatedAngle_ + incrementalAngle,
                -MaximumAccumulatedAngle,
                MaximumAccumulatedAngle);
            previousRadial_ = radial;
            angle = accumulatedAngle_;
        }
        else
        {
            var pointerX = point.X - initialPointer_.X;
            var pointerY = point.Y - initialPointer_.Y;
            angle = Math.Clamp(
                (pointerX * screenTangent_.X + pointerY * screenTangent_.Y) /
                ViewportGizmoMath.LengthPixels,
                -MaximumAccumulatedAngle,
                MaximumAccumulatedAngle);
            if (!float.IsFinite(angle))
            {
                return false;
            }
            accumulatedAngle_ = angle;
        }

        var halfAngle = angle * 0.5f;
        var sineHalfAngle = MathF.Sin(halfAngle);
        var delta = new Quaternion(
            axisDirection_.X * sineHalfAngle,
            axisDirection_.Y * sineHalfAngle,
            axisDirection_.Z * sineHalfAngle,
            MathF.Cos(halfAngle));
        var rotation = Normalize(Multiply(delta, InitialTransform.Rotation));
        if (!IsFinite(rotation))
        {
            return false;
        }

        CurrentTransform = new TransformValue(
            InitialTransform.Position,
            rotation,
            InitialTransform.Scale);
        transform = CurrentTransform;
        return true;
    }

    internal static bool TryCreate(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out ViewportRotateGizmoInteraction interaction)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        interaction = null!;
        if (snapshot.Kind != ViewportTransformGizmoKind.Rotate ||
            !ViewportGizmoMath.TryCreateProjection(
                snapshot.Camera,
                request.Extent,
                out var projection) ||
            !TryHitTest(
                projection,
                snapshot.Transform.Position,
                request,
                out var hit))
        {
            return false;
        }

        var axisDirection = ViewportGizmoMath.AxisDirection(hit.Axis);
        var strategy = RotationDragStrategy.ScreenTangent;
        var radial = hit.Radial;
        if (ViewportGizmoMath.TryCreateRay(projection, request.Point, out var ray) &&
            MathF.Abs(ViewportGizmoMath.Dot(ray.Direction, axisDirection)) >=
                MinimumPlaneAlignment &&
            TryRadial(ray, snapshot.Transform.Position, axisDirection, out var planeRadial))
        {
            strategy = RotationDragStrategy.RayPlane;
            radial = planeRadial;
        }

        interaction = new ViewportRotateGizmoInteraction(
            snapshot,
            request.Extent,
            hit.Axis,
            projection,
            axisDirection,
            strategy,
            request.Point,
            hit.ScreenTangent,
            radial);
        return true;
    }

    internal static ViewportGizmoAxis HitTest(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        return snapshot.Kind == ViewportTransformGizmoKind.Rotate &&
            ViewportGizmoMath.TryCreateProjection(
                snapshot.Camera,
                request.Extent,
                out var projection) &&
            TryHitTest(
                projection,
                snapshot.Transform.Position,
                request,
                out var hit)
                ? hit.Axis
                : ViewportGizmoAxis.None;
    }

    private bool TryRadial(ViewportPickPoint point, out Float3 radial)
    {
        radial = default;
        return ViewportGizmoMath.TryCreateRay(projection_, point, out var ray) &&
            TryRadial(ray, InitialTransform.Position, axisDirection_, out radial);
    }

    private static bool TryRadial(
        ViewportGizmoMath.Ray ray,
        Float3 center,
        Float3 axisDirection,
        out Float3 radial)
    {
        radial = default;
        if (!ViewportGizmoMath.TryIntersectPlane(ray, center, axisDirection, out var point))
        {
            return false;
        }

        radial = ViewportGizmoMath.Normalize(ViewportGizmoMath.Subtract(point, center));
        return ViewportGizmoMath.IsFinite(radial);
    }

    private static bool TryHitTest(
        ViewportGizmoMath.Projection projection,
        Float3 center,
        ViewportPickRequest request,
        out RingHit hit)
    {
        hit = default;
        if (request.Point.X < 0 || request.Point.Y < 0 ||
            request.Point.X >= request.Extent.Width ||
            request.Point.Y >= request.Extent.Height ||
            !ViewportGizmoMath.TryWorldLength(projection, center, out var radius) ||
            !ViewportGizmoMath.TryProject(projection, center, out var projectedCenter) ||
            ViewportGizmoMath.DistanceSquared(request.Point, projectedCenter) <=
                request.TolerancePixels * request.TolerancePixels)
        {
            return false;
        }

        var bestDistance = float.PositiveInfinity;
        foreach (var axis in new[]
                 {
                     ViewportGizmoAxis.X,
                     ViewportGizmoAxis.Y,
                     ViewportGizmoAxis.Z,
                 })
        {
            var (basisU, basisV) = ViewportGizmoMath.RingBasis(axis);
            for (var segment = 0; segment < RingSegmentCount; ++segment)
            {
                var angle0 = MathF.Tau * segment / RingSegmentCount;
                var angle1 = MathF.Tau * (segment + 1) / RingSegmentCount;
                var radial0 = RingRadial(basisU, basisV, angle0);
                var radial1 = RingRadial(basisU, basisV, angle1);
                if (!ViewportGizmoMath.TryProject(
                        projection,
                        ViewportGizmoMath.Add(
                            center,
                            ViewportGizmoMath.Scale(radial0, radius)),
                        out var start) ||
                    !ViewportGizmoMath.TryProject(
                        projection,
                        ViewportGizmoMath.Add(
                            center,
                            ViewportGizmoMath.Scale(radial1, radius)),
                        out var end))
                {
                    continue;
                }

                var distance = ViewportGizmoMath.DistanceToSegment(
                    request.Point,
                    start,
                    end,
                    out var segmentPosition);
                if (distance > request.TolerancePixels || distance >= bestDistance)
                {
                    continue;
                }

                var tangentX = end.X - start.X;
                var tangentY = end.Y - start.Y;
                var tangentLength = MathF.Sqrt(tangentX * tangentX + tangentY * tangentY);
                if (!float.IsFinite(tangentLength) ||
                    tangentLength <= ViewportGizmoMath.Epsilon)
                {
                    continue;
                }

                bestDistance = distance;
                hit = new RingHit(
                    axis,
                    ViewportGizmoMath.Normalize(ViewportGizmoMath.Add(
                        ViewportGizmoMath.Scale(radial0, 1.0f - segmentPosition),
                        ViewportGizmoMath.Scale(radial1, segmentPosition))),
                    new ViewportGizmoMath.ProjectedPoint(
                        tangentX / tangentLength,
                        tangentY / tangentLength));
            }
        }

        return hit.Axis != ViewportGizmoAxis.None;
    }

    private static Float3 RingRadial(Float3 basisU, Float3 basisV, float angle) =>
        ViewportGizmoMath.Add(
            ViewportGizmoMath.Scale(basisU, MathF.Cos(angle)),
            ViewportGizmoMath.Scale(basisV, MathF.Sin(angle)));

    private static Quaternion Multiply(Quaternion lhs, Quaternion rhs) => new(
        lhs.W * rhs.X + lhs.X * rhs.W + lhs.Y * rhs.Z - lhs.Z * rhs.Y,
        lhs.W * rhs.Y - lhs.X * rhs.Z + lhs.Y * rhs.W + lhs.Z * rhs.X,
        lhs.W * rhs.Z + lhs.X * rhs.Y - lhs.Y * rhs.X + lhs.Z * rhs.W,
        lhs.W * rhs.W - lhs.X * rhs.X - lhs.Y * rhs.Y - lhs.Z * rhs.Z);

    private static Quaternion Normalize(Quaternion value)
    {
        var length = MathF.Sqrt(
            value.X * value.X + value.Y * value.Y +
            value.Z * value.Z + value.W * value.W);
        return length <= ViewportGizmoMath.Epsilon
            ? new Quaternion(float.NaN, float.NaN, float.NaN, float.NaN)
            : new Quaternion(
                value.X / length,
                value.Y / length,
                value.Z / length,
                value.W / length);
    }

    private static bool IsFinite(Quaternion value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) &&
        float.IsFinite(value.Z) && float.IsFinite(value.W);

    private static float LengthSquared(Quaternion value) =>
        value.X * value.X + value.Y * value.Y +
        value.Z * value.Z + value.W * value.W;

    private enum RotationDragStrategy
    {
        RayPlane,
        ScreenTangent,
    }

    private readonly record struct RingHit(
        ViewportGizmoAxis Axis,
        Float3 Radial,
        ViewportGizmoMath.ProjectedPoint ScreenTangent);
}

public static class ViewportRotateGizmoManipulator
{
    public static ViewportGizmoAxis HitTest(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request) =>
        ViewportRotateGizmoInteraction.HitTest(snapshot, request);

    public static bool TryBegin(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out ViewportRotateGizmoInteraction interaction) =>
        ViewportRotateGizmoInteraction.TryCreate(snapshot, request, out interaction);
}
