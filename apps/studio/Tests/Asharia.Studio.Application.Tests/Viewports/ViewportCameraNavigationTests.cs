using System;
using Asharia.Runtime;
using Asharia.Studio.Application.Viewports;
using Xunit;

namespace Asharia.Studio.Application.Tests.Viewports;

public sealed class ViewportCameraNavigationTests
{
    [Fact]
    public void Orbit_preserves_target_and_distance_without_crossing_the_pole()
    {
        var camera = ViewportCameraSnapshot.DefaultScene;
        var next = ViewportSceneCameraNavigation.Apply(
            camera,
            new ViewportCameraNavigationDelta(
                ViewportCameraNavigationMode.Orbit,
                horizontalFraction: 0.25f,
                verticalFraction: -100.0f,
                aspectRatio: 4.0f / 3.0f));

        Assert.Equal(camera.Target, next.Target);
        AssertClose(Distance(camera.Position, camera.Target), Distance(next.Position, next.Target));
        Assert.NotEqual(camera.Position, next.Position);
        var forward = Normalize(Subtract(next.Target, next.Position));
        var up = Normalize(next.Up);
        var pitch = MathF.Asin(Math.Clamp(Dot(forward, up), -1.0f, 1.0f));
        Assert.InRange(MathF.Abs(pitch), 0, MathF.PI * (89.01f / 180.0f));
    }

    [Fact]
    public void Pan_translates_position_and_target_together()
    {
        var camera = ViewportCameraSnapshot.DefaultScene;
        var next = ViewportSceneCameraNavigation.Apply(
            camera,
            new ViewportCameraNavigationDelta(
                ViewportCameraNavigationMode.Pan,
                horizontalFraction: 0.25f,
                verticalFraction: -0.10f,
                aspectRatio: 16.0f / 9.0f));

        var positionDelta = Subtract(next.Position, camera.Position);
        var targetDelta = Subtract(next.Target, camera.Target);
        AssertClose(positionDelta.X, targetDelta.X);
        AssertClose(positionDelta.Y, targetDelta.Y);
        AssertClose(positionDelta.Z, targetDelta.Z);
        AssertClose(Distance(camera.Position, camera.Target), Distance(next.Position, next.Target));
        Assert.NotEqual(Float3.Zero, positionDelta);
    }

    [Fact]
    public void Dolly_preserves_target_and_clamps_to_camera_clip_range()
    {
        var camera = ViewportCameraSnapshot.DefaultScene;
        var zoomedIn = ViewportSceneCameraNavigation.Apply(
            camera,
            new ViewportCameraNavigationDelta(
                ViewportCameraNavigationMode.Dolly,
                horizontalFraction: 0,
                verticalFraction: -100.0f,
                aspectRatio: 1.0f));
        var zoomedOut = ViewportSceneCameraNavigation.Apply(
            camera,
            new ViewportCameraNavigationDelta(
                ViewportCameraNavigationMode.Dolly,
                horizontalFraction: 0,
                verticalFraction: 100.0f,
                aspectRatio: 1.0f));

        Assert.Equal(camera.Target, zoomedIn.Target);
        Assert.Equal(camera.Target, zoomedOut.Target);
        Assert.InRange(
            Distance(zoomedIn.Position, zoomedIn.Target),
            camera.NearPlane * 2.0f - 1.0e-4f,
            Distance(camera.Position, camera.Target));
        Assert.InRange(
            Distance(zoomedOut.Position, zoomedOut.Target),
            Distance(camera.Position, camera.Target),
            camera.FarPlane * 0.95f);
    }

    [Fact]
    public void Zero_delta_preserves_the_exact_camera_snapshot()
    {
        var camera = ViewportCameraSnapshot.DefaultScene;

        var next = ViewportSceneCameraNavigation.Apply(
            camera,
            new ViewportCameraNavigationDelta(
                ViewportCameraNavigationMode.Orbit,
                horizontalFraction: 0,
                verticalFraction: 0,
                aspectRatio: 1.0f));

        Assert.Same(camera, next);
    }

    [Fact]
    public void Navigation_delta_rejects_invalid_mode_or_surface_facts()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new ViewportCameraNavigationDelta(
            (ViewportCameraNavigationMode)42,
            horizontalFraction: 0,
            verticalFraction: 0,
            aspectRatio: 1.0f));
        Assert.Throws<ArgumentException>(() => new ViewportCameraNavigationDelta(
            ViewportCameraNavigationMode.Pan,
            horizontalFraction: float.NaN,
            verticalFraction: 0,
            aspectRatio: 1.0f));
        Assert.Throws<ArgumentException>(() => new ViewportCameraNavigationDelta(
            ViewportCameraNavigationMode.Dolly,
            horizontalFraction: 0,
            verticalFraction: 1,
            aspectRatio: 0));
    }

    private static Float3 Subtract(Float3 lhs, Float3 rhs) =>
        new(lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z);

    private static float Dot(Float3 lhs, Float3 rhs) =>
        lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;

    private static float Distance(Float3 lhs, Float3 rhs)
    {
        var delta = Subtract(lhs, rhs);
        return MathF.Sqrt(Dot(delta, delta));
    }

    private static Float3 Normalize(Float3 value)
    {
        var length = MathF.Sqrt(Dot(value, value));
        return new Float3(value.X / length, value.Y / length, value.Z / length);
    }

    private static void AssertClose(float expected, float actual) =>
        Assert.InRange(MathF.Abs(expected - actual), 0, 1.0e-4f);
}
