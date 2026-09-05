using System;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Viewports;

public enum ViewportTransformGizmoKind : uint
{
    Translate = 0,
    Rotate = 1,
    Scale = 2,
}

public enum ViewportGizmoAxis : uint
{
    None = 0,
    X = 1,
    Y = 2,
    Z = 3,
}

public sealed record ViewportTransformGizmoState
{
    public ViewportTransformGizmoState(
        ViewportTransformGizmoKind kind,
        Guid objectId,
        TransformValue transform,
        ViewportGizmoAxis hoveredAxis = ViewportGizmoAxis.None,
        ViewportGizmoAxis activeAxis = ViewportGizmoAxis.None)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind));
        }
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Transform Gizmo object id must not be empty.", nameof(objectId));
        }
        if (!IsValidTransform(transform))
        {
            throw new ArgumentException("Transform Gizmo Transform is invalid.", nameof(transform));
        }
        if (!Enum.IsDefined(hoveredAxis))
        {
            throw new ArgumentOutOfRangeException(nameof(hoveredAxis));
        }
        if (!Enum.IsDefined(activeAxis))
        {
            throw new ArgumentOutOfRangeException(nameof(activeAxis));
        }

        Kind = kind;
        ObjectId = objectId;
        Transform = transform;
        HoveredAxis = hoveredAxis;
        ActiveAxis = activeAxis;
    }

    public ViewportTransformGizmoKind Kind { get; }

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
        return ViewportGizmoMath.IsFinite(transform.Position) &&
            ViewportGizmoMath.IsFinite(transform.Scale) &&
            float.IsFinite(rotationLengthSquared) &&
            MathF.Abs(rotationLengthSquared - 1.0f) <= 0.001f;
    }
}

public sealed record ViewportTransformGizmoSnapshot
{
    public ViewportTransformGizmoSnapshot(
        ViewportTransformGizmoKind kind,
        Guid objectId,
        ulong targetRevision,
        ViewportCameraSnapshot camera,
        TransformValue transform)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind));
        }
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Transform Gizmo object id must not be empty.", nameof(objectId));
        }
        if (targetRevision == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(targetRevision));
        }
        ArgumentNullException.ThrowIfNull(camera);
        if (!ViewportTransformGizmoState.IsValidTransform(transform))
        {
            throw new ArgumentException("Transform Gizmo Transform is invalid.", nameof(transform));
        }

        Kind = kind;
        ObjectId = objectId;
        TargetRevision = targetRevision;
        Camera = camera;
        Transform = transform;
    }

    public ViewportTransformGizmoKind Kind { get; }

    public Guid ObjectId { get; }

    public ulong TargetRevision { get; }

    public ViewportCameraSnapshot Camera { get; }

    public TransformValue Transform { get; }
}

public interface IViewportTransformGizmoInteraction
{
    Guid ObjectId { get; }

    ulong ExpectedRevision { get; }

    ViewportGizmoAxis Axis { get; }

    TransformValue CurrentTransform { get; }

    bool HasChanged { get; }

    bool TryUpdate(ViewportPickPoint point, out TransformValue transform);
}

internal static class ViewportGizmoMath
{
    internal const float Epsilon = 1.0e-6f;
    internal const float LengthPixels = 84.0f;

    internal static bool TryCreateProjection(
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

    internal static bool TryWorldLength(
        Projection projection,
        Float3 origin,
        out float worldLength)
    {
        worldLength = 0;
        var cameraDepth = Dot(projection.Forward, Subtract(origin, projection.Position));
        if (!float.IsFinite(cameraDepth) || cameraDepth <= Epsilon)
        {
            return false;
        }

        worldLength = 2.0f * cameraDepth * LengthPixels /
            (projection.VerticalScale * projection.Height);
        return float.IsFinite(worldLength) && worldLength > Epsilon;
    }

    internal static bool TryCreateRay(
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

    internal static bool TryIntersectPlane(
        Ray ray,
        Float3 planePoint,
        Float3 planeNormal,
        out Float3 point)
    {
        point = default;
        var denominator = Dot(ray.Direction, planeNormal);
        if (!float.IsFinite(denominator) || MathF.Abs(denominator) <= Epsilon)
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

    internal static bool TryProject(
        Projection projection,
        Float3 point,
        out ProjectedPoint projected)
    {
        projected = default;
        var relative = Subtract(point, projection.Position);
        var cameraDepth = Dot(projection.Forward, relative);
        if (!float.IsFinite(cameraDepth) || cameraDepth <= Epsilon)
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

    internal static float DistanceToSegment(
        ViewportPickPoint pointer,
        ProjectedPoint start,
        ProjectedPoint end,
        out float segmentPosition)
    {
        var segmentX = end.X - start.X;
        var segmentY = end.Y - start.Y;
        var lengthSquared = segmentX * segmentX + segmentY * segmentY;
        if (lengthSquared <= Epsilon)
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

    internal static Float3 AxisDirection(ViewportGizmoAxis axis) => axis switch
    {
        ViewportGizmoAxis.X => new Float3(1, 0, 0),
        ViewportGizmoAxis.Y => new Float3(0, 1, 0),
        ViewportGizmoAxis.Z => new Float3(0, 0, 1),
        _ => throw new ArgumentOutOfRangeException(nameof(axis), axis, null),
    };

    internal static Float3 Rotate(Quaternion rotation, Float3 value)
    {
        var imaginary = new Float3(rotation.X, rotation.Y, rotation.Z);
        var twiceCross = Scale(Cross(imaginary, value), 2.0f);
        return Add(
            value,
            Add(
                Scale(twiceCross, rotation.W),
                Cross(imaginary, twiceCross)));
    }

    internal static (Float3 U, Float3 V) RingBasis(ViewportGizmoAxis axis) => axis switch
    {
        ViewportGizmoAxis.X => (new Float3(0, 1, 0), new Float3(0, 0, 1)),
        ViewportGizmoAxis.Y => (new Float3(0, 0, 1), new Float3(1, 0, 0)),
        ViewportGizmoAxis.Z => (new Float3(1, 0, 0), new Float3(0, 1, 0)),
        _ => throw new ArgumentOutOfRangeException(nameof(axis), axis, null),
    };

    internal static Float3 Add(Float3 lhs, Float3 rhs) =>
        new(lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z);

    internal static Float3 Subtract(Float3 lhs, Float3 rhs) =>
        new(lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z);

    internal static Float3 Scale(Float3 value, float scalar) =>
        new(value.X * scalar, value.Y * scalar, value.Z * scalar);

    internal static float Dot(Float3 lhs, Float3 rhs) =>
        lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;

    internal static Float3 Cross(Float3 lhs, Float3 rhs) => new(
        lhs.Y * rhs.Z - lhs.Z * rhs.Y,
        lhs.Z * rhs.X - lhs.X * rhs.Z,
        lhs.X * rhs.Y - lhs.Y * rhs.X);

    internal static Float3 Normalize(Float3 value)
    {
        var length = MathF.Sqrt(Dot(value, value));
        return length <= Epsilon
            ? new Float3(float.NaN, float.NaN, float.NaN)
            : Scale(value, 1.0f / length);
    }

    internal static bool IsFinite(Float3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    internal static float DistanceSquared(Float3 lhs, Float3 rhs)
    {
        var x = lhs.X - rhs.X;
        var y = lhs.Y - rhs.Y;
        var z = lhs.Z - rhs.Z;
        return x * x + y * y + z * z;
    }

    internal static float DistanceSquared(ViewportPickPoint lhs, ProjectedPoint rhs)
    {
        var x = lhs.X - rhs.X;
        var y = lhs.Y - rhs.Y;
        return x * x + y * y;
    }

    internal readonly record struct Projection(
        Float3 Position,
        Float3 Right,
        Float3 Up,
        Float3 Forward,
        float HorizontalScale,
        float VerticalScale,
        float Width,
        float Height);

    internal readonly record struct ProjectedPoint(float X, float Y);

    internal readonly record struct Ray(Float3 Origin, Float3 Direction);
}
