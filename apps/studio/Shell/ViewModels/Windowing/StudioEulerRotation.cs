using System;
using Asharia.Runtime;

namespace Editor.Shell.ViewModels.Windowing;

internal readonly record struct StudioEulerDegrees(
    double X,
    double Y,
    double Z);

internal static class StudioEulerRotation
{
    private const double DegreesToRadians = Math.PI / 180.0;
    private const double RadiansToDegrees = 180.0 / Math.PI;
    private const double FullTurnRadians = 2.0 * Math.PI;
    private const double UnitLengthSquaredTolerance = 1.0e-3;
    private const double GimbalThreshold = 1.0e-6;
    private const double EquivalentAngularTolerance = 2.0e-6;

    internal static Quaternion QuaternionFromEulerDegreesYxz(StudioEulerDegrees euler)
    {
        var rotation = QuaternionFromEulerRadiansYxz(new EulerRadians(
            euler.X * DegreesToRadians,
            euler.Y * DegreesToRadians,
            euler.Z * DegreesToRadians));
        return new Quaternion(
            (float)rotation.X,
            (float)rotation.Y,
            (float)rotation.Z,
            (float)rotation.W);
    }

    internal static bool TryClosestEquivalentEulerDegreesYxz(
        Quaternion rotation,
        StudioEulerDegrees hint,
        out StudioEulerDegrees result)
    {
        result = default;
        if (!TryNormalize(rotation, out var target) || !IsFinite(hint))
        {
            return false;
        }

        var xx = target.X * target.X;
        var yy = target.Y * target.Y;
        var zz = target.Z * target.Z;
        var xy = target.X * target.Y;
        var xz = target.X * target.Z;
        var yz = target.Y * target.Z;
        var wx = target.W * target.X;
        var wy = target.W * target.Y;
        var wz = target.W * target.Z;

        var m00 = 1.0 - (2.0 * (yy + zz));
        var m02 = 2.0 * (xz + wy);
        var m10 = 2.0 * (xy + wz);
        var m11 = 1.0 - (2.0 * (xx + zz));
        var m12 = 2.0 * (yz - wx);
        var m20 = 2.0 * (xz - wy);
        var m22 = 1.0 - (2.0 * (xx + yy));

        var hintRadians = new EulerRadians(
            hint.X * DegreesToRadians,
            hint.Y * DegreesToRadians,
            hint.Z * DegreesToRadians);
        var sinX = Math.Clamp(-m12, -1.0, 1.0);
        var cosXMagnitude = Hypotenuse(m02, m22);
        var hasBest = false;
        var best = default(EulerRadians);
        var bestDistanceSquared = double.PositiveInfinity;

        // The hint may already be an exact (possibly multi-turn) representation
        // of the incoming float quaternion. Keep it only after proving that fact.
        ConsiderCandidate(
            hintRadians,
            hintRadians,
            target,
            ref hasBest,
            ref best,
            ref bestDistanceSquared);

        if (cosXMagnitude <= GimbalThreshold)
        {
            var lockedX = sinX < 0.0 ? -Math.PI * 0.5 : Math.PI * 0.5;
            var combined = Math.Atan2(-m20, m00);
            ConsiderCandidate(
                ClosestGimbalCandidate(lockedX, combined, hintRadians),
                hintRadians,
                target,
                ref hasBest,
                ref best,
                ref bestDistanceSquared);
        }

        // R = Ry * Rx * Rz has two regular Euler branches. atan2(sinX, |cosX|)
        // keeps X and the branch decision derived from the same matrix values.
        var primaryX = Math.Atan2(sinX, cosXMagnitude);
        var primaryY = Math.Atan2(m02, m22);
        var primaryZ = Math.Atan2(m10, m11);
        ConsiderCandidate(
            UnwrapNear(
                new EulerRadians(primaryX, primaryY, primaryZ),
                hintRadians),
            hintRadians,
            target,
            ref hasBest,
            ref best,
            ref bestDistanceSquared);
        ConsiderCandidate(
            UnwrapNear(
                new EulerRadians(
                    Math.PI - primaryX,
                    primaryY + Math.PI,
                    primaryZ + Math.PI),
                hintRadians),
            hintRadians,
            target,
            ref hasBest,
            ref best,
            ref bestDistanceSquared);

        if (!hasBest)
        {
            return false;
        }

        result = new StudioEulerDegrees(
            best.X * RadiansToDegrees,
            best.Y * RadiansToDegrees,
            best.Z * RadiansToDegrees);
        return true;
    }

    internal static bool AreEquivalent(Quaternion lhs, Quaternion rhs)
    {
        if (!TryNormalize(lhs, out var normalizedLhs) ||
            !TryNormalize(rhs, out var normalizedRhs))
        {
            return false;
        }

        return AngularDistance(normalizedLhs, normalizedRhs) <=
            EquivalentAngularTolerance;
    }

    private static EulerRadians ClosestGimbalCandidate(
        double lockedX,
        double combined,
        EulerRadians hint)
    {
        var x = UnwrapNear(lockedX, hint.X);
        if (lockedX > 0.0)
        {
            // At X = +90 degrees, only Y - Z is observable. Project the hint
            // onto that family instead of arbitrarily forcing either axis to zero.
            var difference = UnwrapNear(combined, hint.Y - hint.Z);
            return new EulerRadians(
                x,
                (hint.Y + hint.Z + difference) * 0.5,
                (hint.Y + hint.Z - difference) * 0.5);
        }

        // At X = -90 degrees, only Y + Z is observable.
        var sum = UnwrapNear(combined, hint.Y + hint.Z);
        return new EulerRadians(
            x,
            (hint.Y - hint.Z + sum) * 0.5,
            (-hint.Y + hint.Z + sum) * 0.5);
    }

    private static void ConsiderCandidate(
        EulerRadians candidate,
        EulerRadians hint,
        QuaternionDouble target,
        ref bool hasBest,
        ref EulerRadians best,
        ref double bestDistanceSquared)
    {
        if (!IsFinite(candidate))
        {
            return;
        }

        var recomposed = QuaternionFromEulerRadiansYxz(candidate);
        if (AngularDistance(recomposed, target) > EquivalentAngularTolerance)
        {
            return;
        }

        var distanceSquared = DistanceSquared(candidate, hint);
        if (hasBest && distanceSquared >= bestDistanceSquared)
        {
            return;
        }

        hasBest = true;
        best = candidate;
        bestDistanceSquared = distanceSquared;
    }

    private static QuaternionDouble QuaternionFromEulerRadiansYxz(EulerRadians euler)
    {
        var halfX = euler.X * 0.5;
        var halfY = euler.Y * 0.5;
        var halfZ = euler.Z * 0.5;
        var sinX = Math.Sin(halfX);
        var cosX = Math.Cos(halfX);
        var sinY = Math.Sin(halfY);
        var cosY = Math.Cos(halfY);
        var sinZ = Math.Sin(halfZ);
        var cosZ = Math.Cos(halfZ);

        return new QuaternionDouble(
            (cosY * sinX * cosZ) + (sinY * cosX * sinZ),
            (sinY * cosX * cosZ) - (cosY * sinX * sinZ),
            (cosY * cosX * sinZ) - (sinY * sinX * cosZ),
            (cosY * cosX * cosZ) + (sinY * sinX * sinZ));
    }

    private static bool TryNormalize(
        Quaternion rotation,
        out QuaternionDouble normalized)
    {
        normalized = default;
        if (!float.IsFinite(rotation.X) ||
            !float.IsFinite(rotation.Y) ||
            !float.IsFinite(rotation.Z) ||
            !float.IsFinite(rotation.W))
        {
            return false;
        }

        var x = (double)rotation.X;
        var y = (double)rotation.Y;
        var z = (double)rotation.Z;
        var w = (double)rotation.W;
        var lengthSquared = (x * x) + (y * y) + (z * z) + (w * w);
        if (!double.IsFinite(lengthSquared) ||
            lengthSquared <= double.Epsilon ||
            Math.Abs(lengthSquared - 1.0) > UnitLengthSquaredTolerance)
        {
            return false;
        }

        var inverseLength = 1.0 / Math.Sqrt(lengthSquared);
        normalized = new QuaternionDouble(
            x * inverseLength,
            y * inverseLength,
            z * inverseLength,
            w * inverseLength);
        return true;
    }

    private static double AngularDistance(
        QuaternionDouble lhs,
        QuaternionDouble rhs)
    {
        var dot = Math.Abs(
            (lhs.X * rhs.X) +
            (lhs.Y * rhs.Y) +
            (lhs.Z * rhs.Z) +
            (lhs.W * rhs.W));
        dot = Math.Clamp(dot, 0.0, 1.0);
        return 2.0 * Math.Acos(dot);
    }

    private static EulerRadians UnwrapNear(EulerRadians value, EulerRadians hint) =>
        new(
            UnwrapNear(value.X, hint.X),
            UnwrapNear(value.Y, hint.Y),
            UnwrapNear(value.Z, hint.Z));

    private static double UnwrapNear(double value, double hint) =>
        value +
        (FullTurnRadians * Math.Round(
            (hint - value) / FullTurnRadians,
            MidpointRounding.AwayFromZero));

    private static double DistanceSquared(EulerRadians lhs, EulerRadians rhs)
    {
        var x = lhs.X - rhs.X;
        var y = lhs.Y - rhs.Y;
        var z = lhs.Z - rhs.Z;
        return (x * x) + (y * y) + (z * z);
    }

    private static double Hypotenuse(double x, double y)
    {
        x = Math.Abs(x);
        y = Math.Abs(y);
        var maximum = Math.Max(x, y);
        if (maximum == 0.0)
        {
            return 0.0;
        }

        var minimumRatio = Math.Min(x, y) / maximum;
        return maximum * Math.Sqrt(1.0 + (minimumRatio * minimumRatio));
    }

    private static bool IsFinite(StudioEulerDegrees value) =>
        double.IsFinite(value.X) &&
        double.IsFinite(value.Y) &&
        double.IsFinite(value.Z);

    private static bool IsFinite(EulerRadians value) =>
        double.IsFinite(value.X) &&
        double.IsFinite(value.Y) &&
        double.IsFinite(value.Z);

    private readonly record struct EulerRadians(
        double X,
        double Y,
        double Z);

    private readonly record struct QuaternionDouble(
        double X,
        double Y,
        double Z,
        double W);
}
