using System;
using Asharia.Runtime;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Windowing;

public sealed class StudioEulerRotationTests
{
    [Fact]
    public void Closest_equivalent_euler_preserves_hint_winding()
    {
        var target = Rotation(7.0, 21.0, -29.0);

        var success = StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
            target,
            new StudioEulerDegrees(365.0, 20.0, -30.0),
            out var actual);

        Assert.True(success);
        AssertEulerNear(new StudioEulerDegrees(367.0, 21.0, -29.0), actual);
        AssertEquivalent(target, Rotation(actual));
    }

    [Fact]
    public void Closest_equivalent_euler_can_select_second_regular_branch()
    {
        var expected = new StudioEulerDegrees(120.0, 40.0, -25.0);
        var target = Rotation(60.0, -140.0, 155.0);

        var success = StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
            target,
            new StudioEulerDegrees(121.0, 39.0, -24.0),
            out var actual);

        Assert.True(success);
        AssertEulerNear(expected, actual);
        AssertEquivalent(target, Rotation(actual));
    }

    [Fact]
    public void Negated_quaternion_selects_the_same_euler_representation()
    {
        var target = Rotation(30.0, 220.0, -15.0);
        var negated = new Quaternion(
            -target.X,
            -target.Y,
            -target.Z,
            -target.W);
        var hint = new StudioEulerDegrees(31.0, 219.0, -14.0);

        Assert.True(StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
            target,
            hint,
            out var positiveResult));
        Assert.True(StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
            negated,
            hint,
            out var negativeResult));

        AssertEulerNear(positiveResult, negativeResult);
        AssertEquivalent(target, negated);
    }

    [Fact]
    public void Positive_gimbal_lock_projects_to_nearest_hint_family()
    {
        var target = Rotation(90.0, 50.0, -20.0);
        var hint = new StudioEulerDegrees(90.0, 47.0, -20.0);

        var success = StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
            target,
            hint,
            out var actual);

        Assert.True(success);
        AssertEulerNear(new StudioEulerDegrees(90.0, 48.5, -21.5), actual, 2.0e-5);
        AssertEquivalent(target, Rotation(actual));
    }

    [Fact]
    public void Negative_gimbal_lock_projects_to_nearest_hint_family()
    {
        var target = Rotation(-90.0, 50.0, -20.0);
        var hint = new StudioEulerDegrees(-90.0, 47.0, -20.0);

        var success = StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
            target,
            hint,
            out var actual);

        Assert.True(success);
        AssertEulerNear(new StudioEulerDegrees(-90.0, 48.5, -18.5), actual, 2.0e-5);
        AssertEquivalent(target, Rotation(actual));
    }

    [Theory]
    [InlineData(89.999, 50.0, -20.0)]
    [InlineData(90.001, 50.0, -20.0)]
    [InlineData(-89.999, 50.0, -20.0)]
    [InlineData(-90.001, 50.0, -20.0)]
    public void Near_gimbal_lock_remains_on_the_hint_branch(
        double x,
        double y,
        double z)
    {
        var hint = new StudioEulerDegrees(x, y, z);
        var target = Rotation(hint);

        var success = StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
            target,
            hint,
            out var actual);

        Assert.True(success);
        AssertEulerNear(hint, actual, 0.01);
        AssertEquivalent(target, Rotation(actual));
    }

    [Fact]
    public void Random_euler_inputs_round_trip_to_the_same_orientation()
    {
        var random = new Random(0x366);
        for (var index = 0; index < 2_000; ++index)
        {
            var source = new StudioEulerDegrees(
                Next(random, -720.0, 720.0),
                Next(random, -720.0, 720.0),
                Next(random, -720.0, 720.0));
            var target = Rotation(source);
            var hint = new StudioEulerDegrees(
                source.X + Next(random, -30.0, 30.0) + (360.0 * random.Next(-2, 3)),
                source.Y + Next(random, -30.0, 30.0) + (360.0 * random.Next(-2, 3)),
                source.Z + Next(random, -30.0, 30.0) + (360.0 * random.Next(-2, 3)));

            var success = StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
                target,
                hint,
                out var actual);

            Assert.True(success);
            AssertEquivalent(target, Rotation(actual));
        }
    }

    [Theory]
    [InlineData(float.NaN, 0.0F, 0.0F, 1.0F)]
    [InlineData(0.0F, 0.0F, 0.0F, 0.0F)]
    [InlineData(0.0F, 0.0F, 0.0F, 2.0F)]
    public void Invalid_quaternion_is_rejected(float x, float y, float z, float w)
    {
        var success = StudioEulerRotation.TryClosestEquivalentEulerDegreesYxz(
            new Quaternion(x, y, z, w),
            new StudioEulerDegrees(0.0, 0.0, 0.0),
            out _);

        Assert.False(success);
    }

    private static Quaternion Rotation(double x, double y, double z) =>
        Rotation(new StudioEulerDegrees(x, y, z));

    private static Quaternion Rotation(StudioEulerDegrees euler) =>
        StudioEulerRotation.QuaternionFromEulerDegreesYxz(euler);

    private static void AssertEquivalent(Quaternion expected, Quaternion actual) =>
        Assert.True(
            StudioEulerRotation.AreEquivalent(expected, actual),
            $"Expected ({expected.X}, {expected.Y}, {expected.Z}, {expected.W}) " +
            $"to be rotation-equivalent to ({actual.X}, {actual.Y}, {actual.Z}, {actual.W}).");

    private static void AssertEulerNear(
        StudioEulerDegrees expected,
        StudioEulerDegrees actual,
        double tolerance = 1.0e-5)
    {
        Assert.InRange(actual.X, expected.X - tolerance, expected.X + tolerance);
        Assert.InRange(actual.Y, expected.Y - tolerance, expected.Y + tolerance);
        Assert.InRange(actual.Z, expected.Z - tolerance, expected.Z + tolerance);
    }

    private static double Next(Random random, double minimum, double maximum) =>
        minimum + (random.NextDouble() * (maximum - minimum));
}
