using System;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Viewports;

public enum ViewportGizmoAxis : uint
{
    None = 0,
    X = 1,
    Y = 2,
    Z = 3,
}

public sealed record ViewportTranslateGizmoState
{
    public ViewportTranslateGizmoState(
        Guid objectId,
        TransformValue transform,
        ViewportGizmoAxis hoveredAxis = ViewportGizmoAxis.None,
        ViewportGizmoAxis activeAxis = ViewportGizmoAxis.None)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Translate Gizmo object id must not be empty.", nameof(objectId));
        }
        if (!IsValidTransform(transform))
        {
            throw new ArgumentException("Translate Gizmo Transform is invalid.", nameof(transform));
        }
        if (!Enum.IsDefined(hoveredAxis))
        {
            throw new ArgumentOutOfRangeException(nameof(hoveredAxis));
        }
        if (!Enum.IsDefined(activeAxis))
        {
            throw new ArgumentOutOfRangeException(nameof(activeAxis));
        }

        ObjectId = objectId;
        Transform = transform;
        HoveredAxis = hoveredAxis;
        ActiveAxis = activeAxis;
    }

    public Guid ObjectId { get; }

    public TransformValue Transform { get; }

    public ViewportGizmoAxis HoveredAxis { get; }

    public ViewportGizmoAxis ActiveAxis { get; }

    internal static bool IsValidTransform(TransformValue transform)
    {
        var rotationLengthSquared =
            transform.Rotation.X * transform.Rotation.X +
            transform.Rotation.Y * transform.Rotation.Y +
            transform.Rotation.Z * transform.Rotation.Z +
            transform.Rotation.W * transform.Rotation.W;
        return IsFinite(transform.Position) && IsFinite(transform.Scale) &&
            float.IsFinite(rotationLengthSquared) &&
            MathF.Abs(rotationLengthSquared - 1.0f) <= 0.001f;
    }

    private static bool IsFinite(Float3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);
}

public sealed record ViewportTranslateGizmoSnapshot
{
    public ViewportTranslateGizmoSnapshot(
        Guid objectId,
        ulong targetRevision,
        ViewportCameraSnapshot camera,
        TransformValue transform)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Translate Gizmo object id must not be empty.", nameof(objectId));
        }
        if (targetRevision == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(targetRevision));
        }
        ArgumentNullException.ThrowIfNull(camera);
        if (!ViewportTranslateGizmoState.IsValidTransform(transform))
        {
            throw new ArgumentException("Translate Gizmo Transform is invalid.", nameof(transform));
        }

        ObjectId = objectId;
        TargetRevision = targetRevision;
        Camera = camera;
        Transform = transform;
    }

    public Guid ObjectId { get; }

    public ulong TargetRevision { get; }

    public ViewportCameraSnapshot Camera { get; }

    public TransformValue Transform { get; }
}

public sealed class ViewportTranslateGizmoInteraction
{
    private const float MaximumDragDistanceFactor = 0.95f;
    private readonly Projection projection_;
    private readonly Float3 axisDirection_;
    private readonly Float3 dragPlaneNormal_;
    private readonly float initialAxisParameter_;

    private ViewportTranslateGizmoInteraction(
        ViewportTranslateGizmoSnapshot snapshot,
        ViewportExtent extent,
        ViewportGizmoAxis axis,
        Projection projection,
        Float3 axisDirection,
        Float3 dragPlaneNormal,
        float initialAxisParameter)
    {
        ObjectId = snapshot.ObjectId;
        ExpectedRevision = snapshot.TargetRevision;
        Extent = extent;
        Axis = axis;
        InitialTransform = snapshot.Transform;
        CurrentTransform = snapshot.Transform;
        projection_ = projection;
        axisDirection_ = axisDirection;
        dragPlaneNormal_ = dragPlaneNormal;
        initialAxisParameter_ = initialAxisParameter;
        MaximumDragDistance = snapshot.Camera.FarPlane * MaximumDragDistanceFactor;
    }

    public Guid ObjectId { get; }

    public ulong ExpectedRevision { get; }

    public ViewportExtent Extent { get; }

    public ViewportGizmoAxis Axis { get; }

    public TransformValue InitialTransform { get; }

    public TransformValue CurrentTransform { get; private set; }

    public bool HasChanged =>
        DistanceSquared(InitialTransform.Position, CurrentTransform.Position) > 1.0e-10f;

    private float MaximumDragDistance { get; }

    public bool TryUpdate(ViewportPickPoint point, out TransformValue transform)
    {
        transform = CurrentTransform;
        if (!TryCreateRay(projection_, point, out var ray) ||
            !TryIntersectPlane(
                ray,
                InitialTransform.Position,
                dragPlaneNormal_,
                out var pointOnPlane))
        {
            return false;
        }

        var axisParameter = Dot(
            Subtract(pointOnPlane, InitialTransform.Position),
            axisDirection_);
        var delta = Math.Clamp(
            axisParameter - initialAxisParameter_,
            -MaximumDragDistance,
            MaximumDragDistance);
        var position = Add(InitialTransform.Position, Scale(axisDirection_, delta));
        if (!IsFinite(position))
        {
            return false;
        }

        CurrentTransform = new TransformValue(
            position,
            InitialTransform.Rotation,
            InitialTransform.Scale);
        transform = CurrentTransform;
        return true;
    }

    internal static bool TryCreate(
        ViewportTranslateGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out ViewportTranslateGizmoInteraction interaction)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        interaction = null!;
        if (!TryCreateProjection(snapshot.Camera, request.Extent, out var projection) ||
            !TryHitTest(projection, snapshot.Transform.Position, request, out var axis))
        {
            return false;
        }

        var axisDirection = AxisDirection(axis);
        var dragPlaneNormal = Normalize(Subtract(
            projection.Forward,
            Scale(axisDirection, Dot(projection.Forward, axisDirection))));
        if (!IsFinite(dragPlaneNormal) ||
            !TryCreateRay(projection, request.Point, out var ray) ||
            !TryIntersectPlane(
                ray,
                snapshot.Transform.Position,
                dragPlaneNormal,
                out var pointOnPlane))
        {
            return false;
        }

        var initialAxisParameter = Dot(
            Subtract(pointOnPlane, snapshot.Transform.Position),
            axisDirection);
        interaction = new ViewportTranslateGizmoInteraction(
            snapshot,
            request.Extent,
            axis,
            projection,
            axisDirection,
            dragPlaneNormal,
            initialAxisParameter);
        return true;
    }

    private static bool TryIntersectPlane(
        Ray ray,
        Float3 planePoint,
        Float3 planeNormal,
        out Float3 point)
    {
        point = default;
        var denominator = Dot(ray.Direction, planeNormal);
        if (!float.IsFinite(denominator) || MathF.Abs(denominator) <= GizmoEpsilon)
        {
            return false;
        }

        var distance = Dot(Subtract(planePoint, ray.Origin), planeNormal) / denominator;
        if (!float.IsFinite(distance) || distance < 0)
        {
            return false;
        }

        point = Add(ray.Origin, Scale(ray.Direction, distance));
        return IsFinite(point);
    }

    private const float GizmoEpsilon = 1.0e-6f;
    private const float GizmoLengthPixels = 84.0f;
    private const float MinimumPickSegmentPosition = 0.12f;

    private static bool TryHitTest(
        Projection projection,
        Float3 origin,
        ViewportPickRequest request,
        out ViewportGizmoAxis axis)
    {
        axis = ViewportGizmoAxis.None;
        if (request.Point.X < 0 || request.Point.Y < 0 ||
            request.Point.X >= request.Extent.Width ||
            request.Point.Y >= request.Extent.Height ||
            !TryWorldLength(projection, origin, out var worldLength) ||
            !TryProject(projection, origin, out var start))
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
            var endpoint = Add(origin, Scale(AxisDirection(candidateAxis), worldLength));
            if (!TryProject(projection, endpoint, out var end) ||
                DistanceSquared(start, end) <= GizmoEpsilon)
            {
                continue;
            }

            var distance = DistanceToSegment(request.Point, start, end, out var segmentPosition);
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

    internal static ViewportGizmoAxis HitTest(
        ViewportTranslateGizmoSnapshot snapshot,
        ViewportPickRequest request)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        return TryCreateProjection(snapshot.Camera, request.Extent, out var projection) &&
            TryHitTest(projection, snapshot.Transform.Position, request, out var axis)
                ? axis
                : ViewportGizmoAxis.None;
    }

    private static bool TryCreateProjection(
        ViewportCameraSnapshot camera,
        ViewportExtent extent,
        out Projection projection)
    {
        projection = default;
        var forward = Normalize(Subtract(camera.Target, camera.Position));
        var right = Normalize(Cross(camera.Up, forward));
        var cameraUp = Normalize(Cross(forward, right));
        if (!IsFinite(forward) || !IsFinite(right) || !IsFinite(cameraUp))
        {
            return false;
        }

        var focalLength = 1.0f / MathF.Tan(camera.FieldOfViewRadians * 0.5f);
        var aspectRatio = (float)extent.Width / extent.Height;
        var horizontalScale = camera.FieldOfViewAxis switch
        {
            ViewportFieldOfViewAxis.MaintainHorizontal => focalLength,
            ViewportFieldOfViewAxis.MaintainVertical => focalLength / aspectRatio,
            _ => 0,
        };
        var verticalScale = camera.FieldOfViewAxis switch
        {
            ViewportFieldOfViewAxis.MaintainHorizontal => focalLength * aspectRatio,
            ViewportFieldOfViewAxis.MaintainVertical => focalLength,
            _ => 0,
        };
        if (!float.IsFinite(horizontalScale) || !float.IsFinite(verticalScale) ||
            horizontalScale <= 0 || verticalScale <= 0)
        {
            return false;
        }

        projection = new Projection(
            camera.Position,
            right,
            cameraUp,
            forward,
            horizontalScale,
            verticalScale,
            extent.Width,
            extent.Height);
        return true;
    }

    private static bool TryWorldLength(
        Projection projection,
        Float3 origin,
        out float worldLength)
    {
        worldLength = 0;
        var cameraDepth = Dot(projection.Forward, Subtract(origin, projection.Position));
        if (!float.IsFinite(cameraDepth) || cameraDepth <= GizmoEpsilon)
        {
            return false;
        }

        worldLength = 2.0f * cameraDepth * GizmoLengthPixels /
            (projection.VerticalScale * projection.Height);
        return float.IsFinite(worldLength) && worldLength > GizmoEpsilon;
    }

    private static bool TryCreateRay(
        Projection projection,
        ViewportPickPoint pointer,
        out Ray ray)
    {
        ray = default;
        var ndcX = (pointer.X * 2.0f / projection.Width) - 1.0f;
        var ndcY = 1.0f - (pointer.Y * 2.0f / projection.Height);
        var direction = Normalize(Add(
            projection.Forward,
            Add(
                Scale(projection.Right, ndcX / projection.HorizontalScale),
                Scale(projection.Up, ndcY / projection.VerticalScale))));
        if (!IsFinite(direction))
        {
            return false;
        }

        ray = new Ray(projection.Position, direction);
        return true;
    }

    private static bool TryProject(
        Projection projection,
        Float3 point,
        out ProjectedPoint projected)
    {
        projected = default;
        var relative = Subtract(point, projection.Position);
        var cameraDepth = Dot(projection.Forward, relative);
        if (!float.IsFinite(cameraDepth) || cameraDepth <= GizmoEpsilon)
        {
            return false;
        }

        var ndcX = projection.HorizontalScale * Dot(projection.Right, relative) / cameraDepth;
        var ndcY = projection.VerticalScale * Dot(projection.Up, relative) / cameraDepth;
        if (!float.IsFinite(ndcX) || !float.IsFinite(ndcY))
        {
            return false;
        }

        projected = new ProjectedPoint(
            ((ndcX + 1.0f) * 0.5f) * projection.Width,
            ((1.0f - ndcY) * 0.5f) * projection.Height);
        return true;
    }

    private static float DistanceToSegment(
        ViewportPickPoint pointer,
        ProjectedPoint start,
        ProjectedPoint end,
        out float segmentPosition)
    {
        var segmentX = end.X - start.X;
        var segmentY = end.Y - start.Y;
        var lengthSquared = segmentX * segmentX + segmentY * segmentY;
        if (lengthSquared <= GizmoEpsilon)
        {
            segmentPosition = 0;
            return MathF.Sqrt(DistanceSquared(pointer, start));
        }

        segmentPosition = ((pointer.X - start.X) * segmentX +
            (pointer.Y - start.Y) * segmentY) / lengthSquared;
        segmentPosition = Math.Clamp(segmentPosition, 0.0f, 1.0f);
        var nearest = new ProjectedPoint(
            start.X + segmentX * segmentPosition,
            start.Y + segmentY * segmentPosition);
        return MathF.Sqrt(DistanceSquared(pointer, nearest));
    }

    private static Float3 AxisDirection(ViewportGizmoAxis axis) => axis switch
    {
        ViewportGizmoAxis.X => new Float3(1, 0, 0),
        ViewportGizmoAxis.Y => new Float3(0, 1, 0),
        ViewportGizmoAxis.Z => new Float3(0, 0, 1),
        _ => throw new ArgumentOutOfRangeException(nameof(axis), axis, null),
    };

    private static Float3 Add(Float3 lhs, Float3 rhs) =>
        new(lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z);

    private static Float3 Subtract(Float3 lhs, Float3 rhs) =>
        new(lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z);

    private static Float3 Scale(Float3 value, float scalar) =>
        new(value.X * scalar, value.Y * scalar, value.Z * scalar);

    private static float Dot(Float3 lhs, Float3 rhs) =>
        lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;

    private static Float3 Cross(Float3 lhs, Float3 rhs) => new(
        lhs.Y * rhs.Z - lhs.Z * rhs.Y,
        lhs.Z * rhs.X - lhs.X * rhs.Z,
        lhs.X * rhs.Y - lhs.Y * rhs.X);

    private static Float3 Normalize(Float3 value)
    {
        var length = MathF.Sqrt(Dot(value, value));
        return length <= GizmoEpsilon
            ? new Float3(float.NaN, float.NaN, float.NaN)
            : Scale(value, 1.0f / length);
    }

    private static bool IsFinite(Float3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private static float DistanceSquared(Float3 lhs, Float3 rhs)
    {
        var x = lhs.X - rhs.X;
        var y = lhs.Y - rhs.Y;
        var z = lhs.Z - rhs.Z;
        return x * x + y * y + z * z;
    }

    private static float DistanceSquared(ViewportPickPoint lhs, ProjectedPoint rhs)
    {
        var x = lhs.X - rhs.X;
        var y = lhs.Y - rhs.Y;
        return x * x + y * y;
    }

    private static float DistanceSquared(ProjectedPoint lhs, ProjectedPoint rhs)
    {
        var x = lhs.X - rhs.X;
        var y = lhs.Y - rhs.Y;
        return x * x + y * y;
    }

    private readonly record struct Projection(
        Float3 Position,
        Float3 Right,
        Float3 Up,
        Float3 Forward,
        float HorizontalScale,
        float VerticalScale,
        float Width,
        float Height);

    private readonly record struct ProjectedPoint(float X, float Y);

    private readonly record struct Ray(Float3 Origin, Float3 Direction);
}

public static class ViewportTranslateGizmoManipulator
{
    public static ViewportGizmoAxis HitTest(
        ViewportTranslateGizmoSnapshot snapshot,
        ViewportPickRequest request) =>
        ViewportTranslateGizmoInteraction.HitTest(snapshot, request);

    public static bool TryBegin(
        ViewportTranslateGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out ViewportTranslateGizmoInteraction interaction) =>
        ViewportTranslateGizmoInteraction.TryCreate(snapshot, request, out interaction);
}
