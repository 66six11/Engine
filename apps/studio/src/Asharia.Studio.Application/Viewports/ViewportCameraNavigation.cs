using System;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Viewports;

public enum ViewportCameraNavigationMode
{
    Orbit,
    Pan,
    Dolly,
}

public readonly record struct ViewportCameraNavigationDelta
{
    public ViewportCameraNavigationDelta(
        ViewportCameraNavigationMode mode,
        float horizontalFraction,
        float verticalFraction,
        float aspectRatio)
    {
        if (!Enum.IsDefined(mode))
        {
            throw new ArgumentOutOfRangeException(nameof(mode), mode, null);
        }
        if (!float.IsFinite(horizontalFraction) || !float.IsFinite(verticalFraction) ||
            !float.IsFinite(aspectRatio) || aspectRatio <= 0)
        {
            throw new ArgumentException("Viewport camera navigation values are invalid.");
        }

        Mode = mode;
        HorizontalFraction = horizontalFraction;
        VerticalFraction = verticalFraction;
        AspectRatio = aspectRatio;
    }

    public ViewportCameraNavigationMode Mode { get; }

    public float HorizontalFraction { get; }

    public float VerticalFraction { get; }

    public float AspectRatio { get; }
}

public static class ViewportSceneCameraNavigation
{
    private const float Epsilon = 1.0e-6f;
    private const float MaximumPitchRadians = MathF.PI * (89.0f / 180.0f);
    private const float OrbitRadiansPerSurface = MathF.PI;
    private const float DollyExponentPerSurface = 2.0f;

    public static ViewportCameraSnapshot Apply(
        ViewportCameraSnapshot camera,
        ViewportCameraNavigationDelta delta)
    {
        ArgumentNullException.ThrowIfNull(camera);
        if (delta.HorizontalFraction == 0 && delta.VerticalFraction == 0)
        {
            return camera;
        }

        return delta.Mode switch
        {
            ViewportCameraNavigationMode.Orbit => Orbit(camera, delta),
            ViewportCameraNavigationMode.Pan => Pan(camera, delta),
            ViewportCameraNavigationMode.Dolly => Dolly(camera, delta.VerticalFraction),
            _ => throw new ArgumentOutOfRangeException(nameof(delta), delta.Mode, null),
        };
    }

    private static ViewportCameraSnapshot Orbit(
        ViewportCameraSnapshot camera,
        ViewportCameraNavigationDelta delta)
    {
        if (!TryCreateFrame(camera, out var frame))
        {
            return camera;
        }

        var yawedForward = RotateAroundAxis(
            frame.Forward,
            frame.OrbitUp,
            -delta.HorizontalFraction * OrbitRadiansPerSurface);
        var vertical = Math.Clamp(Dot(yawedForward, frame.OrbitUp), -1.0f, 1.0f);
        var planarForward = Normalize(Subtract(yawedForward, Scale(frame.OrbitUp, vertical)));
        if (!IsFinite(planarForward))
        {
            return camera;
        }

        var pitch = MathF.Asin(vertical);
        var nextPitch = Math.Clamp(
            pitch - delta.VerticalFraction * OrbitRadiansPerSurface,
            -MaximumPitchRadians,
            MaximumPitchRadians);
        var nextForward = Add(
            Scale(planarForward, MathF.Cos(nextPitch)),
            Scale(frame.OrbitUp, MathF.Sin(nextPitch)));
        var nextPosition = Subtract(camera.Target, Scale(nextForward, frame.Distance));
        return Copy(camera, nextPosition, camera.Target);
    }

    private static ViewportCameraSnapshot Pan(
        ViewportCameraSnapshot camera,
        ViewportCameraNavigationDelta delta)
    {
        if (!TryCreateFrame(camera, out var frame))
        {
            return camera;
        }

        var halfFovSpan = frame.Distance * MathF.Tan(camera.FieldOfViewRadians * 0.5f);
        var halfWidth = camera.FieldOfViewAxis == ViewportFieldOfViewAxis.MaintainHorizontal
            ? halfFovSpan
            : halfFovSpan * delta.AspectRatio;
        var halfHeight = camera.FieldOfViewAxis == ViewportFieldOfViewAxis.MaintainHorizontal
            ? halfFovSpan / delta.AspectRatio
            : halfFovSpan;
        var translation = Add(
            Scale(frame.Right, -2.0f * halfWidth * delta.HorizontalFraction),
            Scale(frame.ViewUp, 2.0f * halfHeight * delta.VerticalFraction));
        return Copy(
            camera,
            Add(camera.Position, translation),
            Add(camera.Target, translation));
    }

    private static ViewportCameraSnapshot Dolly(
        ViewportCameraSnapshot camera,
        float verticalFraction)
    {
        if (!TryCreateFrame(camera, out var frame))
        {
            return camera;
        }

        var maximumDistance = camera.FarPlane * 0.95f;
        var minimumDistance = MathF.Min(
            MathF.Max(camera.NearPlane * 2.0f, 0.05f),
            maximumDistance * 0.5f);
        var exponent = Math.Clamp(
            verticalFraction * DollyExponentPerSurface,
            -4.0f,
            4.0f);
        var nextDistance = Math.Clamp(
            frame.Distance * MathF.Exp(exponent),
            minimumDistance,
            maximumDistance);
        if (MathF.Abs(nextDistance - frame.Distance) <= Epsilon)
        {
            return camera;
        }

        return Copy(
            camera,
            Subtract(camera.Target, Scale(frame.Forward, nextDistance)),
            camera.Target);
    }

    private static bool TryCreateFrame(
        ViewportCameraSnapshot camera,
        out CameraFrame frame)
    {
        frame = default;
        var offset = Subtract(camera.Target, camera.Position);
        var distance = Length(offset);
        var forward = Normalize(offset);
        var orbitUp = Normalize(camera.Up);
        var right = Normalize(Cross(orbitUp, forward));
        var viewUp = Normalize(Cross(forward, right));
        if (!float.IsFinite(distance) || distance <= Epsilon ||
            !IsFinite(forward) || !IsFinite(orbitUp) ||
            !IsFinite(right) || !IsFinite(viewUp))
        {
            return false;
        }

        frame = new CameraFrame(distance, forward, orbitUp, right, viewUp);
        return true;
    }

    private static ViewportCameraSnapshot Copy(
        ViewportCameraSnapshot camera,
        Float3 position,
        Float3 target) =>
        new(
            position,
            target,
            camera.Up,
            camera.FieldOfViewRadians,
            camera.FieldOfViewAxis,
            camera.NearPlane,
            camera.FarPlane);

    private static Float3 RotateAroundAxis(Float3 value, Float3 axis, float radians)
    {
        var cosine = MathF.Cos(radians);
        var sine = MathF.Sin(radians);
        return Add(
            Add(
                Scale(value, cosine),
                Scale(Cross(axis, value), sine)),
            Scale(axis, Dot(axis, value) * (1.0f - cosine)));
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

    private static float Length(Float3 value) => MathF.Sqrt(Dot(value, value));

    private static Float3 Normalize(Float3 value)
    {
        var length = Length(value);
        return length <= Epsilon
            ? new Float3(float.NaN, float.NaN, float.NaN)
            : Scale(value, 1.0f / length);
    }

    private static bool IsFinite(Float3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private readonly record struct CameraFrame(
        float Distance,
        Float3 Forward,
        Float3 OrbitUp,
        Float3 Right,
        Float3 ViewUp);
}
