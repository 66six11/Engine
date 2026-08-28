using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Viewports;

public readonly record struct ViewportPickPoint
{
    public ViewportPickPoint(float x, float y)
    {
        if (!float.IsFinite(x) || !float.IsFinite(y))
        {
            throw new ArgumentException("Viewport pick coordinates must be finite.");
        }

        X = x;
        Y = y;
    }

    public float X { get; }

    public float Y { get; }
}

public readonly record struct ViewportPickRequest
{
    public ViewportPickRequest(
        ViewportExtent extent,
        ViewportPickPoint point,
        float tolerancePixels)
    {
        if (!extent.IsRenderable)
        {
            throw new ArgumentOutOfRangeException(nameof(extent));
        }
        if (!float.IsFinite(tolerancePixels) || tolerancePixels < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(tolerancePixels));
        }

        Extent = extent;
        Point = point;
        TolerancePixels = tolerancePixels;
    }

    public ViewportExtent Extent { get; }

    public ViewportPickPoint Point { get; }

    public float TolerancePixels { get; }
}

public sealed record ViewportPickSnapshot
{
    internal ViewportPickSnapshot(
        ViewportSessionId sessionId,
        Guid targetId,
        ulong targetRevision,
        ViewportCameraSnapshot camera,
        IEnumerable<ViewportDebugProxySnapshot> debugProxies,
        int totalDebugProxyCount)
    {
        SessionId = sessionId;
        TargetId = targetId;
        TargetRevision = targetRevision;
        Camera = camera;
        DebugProxies = new ReadOnlyCollection<ViewportDebugProxySnapshot>(
            debugProxies.ToArray());
        TotalDebugProxyCount = totalDebugProxyCount;
    }

    public ViewportSessionId SessionId { get; }

    public Guid TargetId { get; }

    public ulong TargetRevision { get; }

    public ViewportCameraSnapshot Camera { get; }

    public IReadOnlyList<ViewportDebugProxySnapshot> DebugProxies { get; }

    public int TotalDebugProxyCount { get; }

    public bool DebugProxiesTruncated => DebugProxies.Count < TotalDebugProxyCount;
}

public readonly record struct ViewportPickResult
{
    private ViewportPickResult(
        Guid? objectId,
        float cameraDepth,
        float screenDistancePixels)
    {
        ObjectId = objectId;
        CameraDepth = cameraDepth;
        ScreenDistancePixels = screenDistancePixels;
    }

    public bool IsHit => ObjectId.HasValue;

    public Guid? ObjectId { get; }

    public float CameraDepth { get; }

    public float ScreenDistancePixels { get; }

    public static ViewportPickResult Miss { get; } = new(
        objectId: null,
        float.PositiveInfinity,
        float.PositiveInfinity);

    internal static ViewportPickResult Hit(
        Guid objectId,
        float cameraDepth,
        float screenDistancePixels) =>
        new(objectId, cameraDepth, screenDistancePixels);
}

public static class ViewportTransformProxyPicker
{
    private const float ProjectionEpsilon = 0.000001f;

    public static ViewportPickResult Pick(
        ViewportPickSnapshot snapshot,
        ViewportPickRequest request)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (!request.Extent.IsRenderable ||
            !float.IsFinite(request.TolerancePixels) || request.TolerancePixels < 0 ||
            request.Point.X < 0 || request.Point.Y < 0 ||
            request.Point.X >= request.Extent.Width ||
            request.Point.Y >= request.Extent.Height)
        {
            return ViewportPickResult.Miss;
        }

        if (!TryCreateProjection(snapshot.Camera, request.Extent, out var projection))
        {
            return ViewportPickResult.Miss;
        }

        var best = ViewportPickResult.Miss;
        foreach (var proxy in snapshot.DebugProxies)
        {
            if (!TryPickProxy(projection, proxy, request.Point, out var candidate))
            {
                continue;
            }
            if (candidate.ScreenDistancePixels > request.TolerancePixels ||
                !IsBetter(candidate, best))
            {
                continue;
            }

            best = candidate;
        }

        return best;
    }

    private static bool TryPickProxy(
        Projection projection,
        ViewportDebugProxySnapshot proxy,
        ViewportPickPoint pointer,
        out ViewportPickResult result)
    {
        result = ViewportPickResult.Miss;
        var transform = proxy.Transform;
        if (!IsFinite(transform.Position) || !IsFinite(transform.Scale) ||
            !IsNormalized(transform.Rotation))
        {
            return false;
        }

        var axes = new[]
        {
            Scale(Rotate(transform.Rotation, new Float3(1, 0, 0)), transform.Scale.X),
            Scale(Rotate(transform.Rotation, new Float3(0, 1, 0)), transform.Scale.Y),
            Scale(Rotate(transform.Rotation, new Float3(0, 0, 1)), transform.Scale.Z),
        };
        foreach (var axis in axes)
        {
            if (!TryProject(projection, transform.Position, out var start) ||
                !TryProject(projection, Add(transform.Position, axis), out var end))
            {
                continue;
            }
            if (Square(end.X - start.X) + Square(end.Y - start.Y) <= ProjectionEpsilon)
            {
                continue;
            }

            var distance = DistanceToSegment(pointer, start, end, out var segmentPosition);
            var depth = start.CameraDepth +
                (end.CameraDepth - start.CameraDepth) * segmentPosition;
            var candidate = ViewportPickResult.Hit(proxy.ObjectId, depth, distance);
            if (IsBetter(candidate, result))
            {
                result = candidate;
            }
        }

        return result.IsHit;
    }

    private static bool IsBetter(ViewportPickResult candidate, ViewportPickResult current)
    {
        if (!current.IsHit)
        {
            return true;
        }
        if (candidate.CameraDepth != current.CameraDepth)
        {
            return candidate.CameraDepth < current.CameraDepth;
        }
        if (candidate.ScreenDistancePixels != current.ScreenDistancePixels)
        {
            return candidate.ScreenDistancePixels < current.ScreenDistancePixels;
        }
        return candidate.ObjectId!.Value.CompareTo(current.ObjectId!.Value) < 0;
    }

    private static bool TryCreateProjection(
        ViewportCameraSnapshot camera,
        ViewportExtent extent,
        out Projection projection)
    {
        projection = default;
        var forward = Normalize(Subtract(camera.Target, camera.Position));
        var right = Normalize(Cross(camera.Up, forward));
        var cameraUp = Cross(forward, right);
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

    private static bool TryProject(
        Projection projection,
        Float3 point,
        out ProjectedPoint projected)
    {
        projected = default;
        var relative = Subtract(point, projection.Position);
        var cameraDepth = Dot(projection.Forward, relative);
        if (!float.IsFinite(cameraDepth) || cameraDepth <= ProjectionEpsilon)
        {
            return false;
        }

        var ndcX = projection.HorizontalScale * Dot(projection.Right, relative) /
            cameraDepth;
        var ndcY = projection.VerticalScale * Dot(projection.Up, relative) /
            cameraDepth;
        if (!float.IsFinite(ndcX) || !float.IsFinite(ndcY))
        {
            return false;
        }

        projected = new ProjectedPoint(
            ((ndcX + 1.0f) * 0.5f) * projection.Width,
            ((1.0f - ndcY) * 0.5f) * projection.Height,
            cameraDepth);
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
        if (lengthSquared <= ProjectionEpsilon)
        {
            segmentPosition = 0;
            return MathF.Sqrt(
                Square(pointer.X - start.X) + Square(pointer.Y - start.Y));
        }

        segmentPosition = ((pointer.X - start.X) * segmentX +
            (pointer.Y - start.Y) * segmentY) / lengthSquared;
        segmentPosition = Math.Clamp(segmentPosition, 0.0f, 1.0f);
        var nearestX = start.X + segmentX * segmentPosition;
        var nearestY = start.Y + segmentY * segmentPosition;
        return MathF.Sqrt(
            Square(pointer.X - nearestX) + Square(pointer.Y - nearestY));
    }

    private static Float3 Rotate(Quaternion rotation, Float3 value)
    {
        var imaginary = new Float3(rotation.X, rotation.Y, rotation.Z);
        var twiceCross = Scale(Cross(imaginary, value), 2.0f);
        return Add(
            value,
            Add(Scale(twiceCross, rotation.W), Cross(imaginary, twiceCross)));
    }

    private static Float3 Add(Float3 lhs, Float3 rhs) =>
        new(lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z);

    private static Float3 Subtract(Float3 lhs, Float3 rhs) =>
        new(lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z);

    private static Float3 Scale(Float3 value, float scale) =>
        new(value.X * scale, value.Y * scale, value.Z * scale);

    private static float Dot(Float3 lhs, Float3 rhs) =>
        lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;

    private static Float3 Cross(Float3 lhs, Float3 rhs) => new(
        lhs.Y * rhs.Z - lhs.Z * rhs.Y,
        lhs.Z * rhs.X - lhs.X * rhs.Z,
        lhs.X * rhs.Y - lhs.Y * rhs.X);

    private static Float3 Normalize(Float3 value)
    {
        var length = MathF.Sqrt(Dot(value, value));
        return length <= ProjectionEpsilon
            ? new Float3(float.NaN, float.NaN, float.NaN)
            : Scale(value, 1.0f / length);
    }

    private static bool IsFinite(Float3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private static bool IsNormalized(Quaternion value)
    {
        var lengthSquared = value.X * value.X + value.Y * value.Y +
            value.Z * value.Z + value.W * value.W;
        return float.IsFinite(lengthSquared) && MathF.Abs(lengthSquared - 1.0f) <= 0.001f;
    }

    private static float Square(float value) => value * value;

    private readonly record struct Projection(
        Float3 Position,
        Float3 Right,
        Float3 Up,
        Float3 Forward,
        float HorizontalScale,
        float VerticalScale,
        float Width,
        float Height);

    private readonly record struct ProjectedPoint(
        float X,
        float Y,
        float CameraDepth);
}
