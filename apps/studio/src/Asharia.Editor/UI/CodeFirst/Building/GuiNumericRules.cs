using System;
using Asharia.Editor.UI.CodeFirst.Models;

namespace Asharia.Editor.UI.CodeFirst.Building;

internal static class GuiNumericRules
{
    public static void ValidateRange(double minimum, double maximum)
    {
        if (double.IsNaN(minimum) || double.IsInfinity(minimum))
        {
            throw new ArgumentOutOfRangeException(
                nameof(minimum),
                minimum,
                "Minimum must be finite.");
        }

        if (double.IsNaN(maximum) || double.IsInfinity(maximum) || maximum <= minimum)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maximum),
                maximum,
                "Maximum must be finite and greater than minimum.");
        }
    }

    public static double Clamp(
        double value,
        double minimum,
        double maximum,
        string parameterName)
    {
        if (double.IsNaN(value) || double.IsInfinity(value))
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                value,
                "Value must be finite.");
        }

        return Math.Clamp(value, minimum, maximum);
    }

    public static void ValidateBounds(double? minimum, double? maximum)
    {
        if (minimum is { } min && (double.IsNaN(min) || double.IsInfinity(min)))
        {
            throw new ArgumentOutOfRangeException(
                nameof(minimum),
                minimum,
                "Minimum must be finite.");
        }

        if (maximum is { } max && (double.IsNaN(max) || double.IsInfinity(max)))
        {
            throw new ArgumentOutOfRangeException(
                nameof(maximum),
                maximum,
                "Maximum must be finite.");
        }

        if (minimum is { } finiteMin && maximum is { } finiteMax && finiteMax <= finiteMin)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maximum),
                maximum,
                "Maximum must be greater than minimum.");
        }
    }

    public static double ClampToBounds(
        double value,
        double? minimum,
        double? maximum,
        string parameterName)
    {
        if (double.IsNaN(value) || double.IsInfinity(value))
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                value,
                "Value must be finite.");
        }

        if (minimum is { } min && value < min)
        {
            return min;
        }

        if (maximum is { } max && value > max)
        {
            return max;
        }

        return value;
    }

    public static GuiVector3Value ClampVector3ToBounds(
        GuiVector3Value value,
        double? minimum,
        double? maximum,
        string parameterName)
    {
        return new GuiVector3Value(
            ClampToBounds(value.X, minimum, maximum, parameterName),
            ClampToBounds(value.Y, minimum, maximum, parameterName),
            ClampToBounds(value.Z, minimum, maximum, parameterName));
    }

    public static GuiVector2Value ClampVector2ToBounds(
        GuiVector2Value value,
        double? minimum,
        double? maximum,
        string parameterName)
    {
        return new GuiVector2Value(
            ClampToBounds(value.X, minimum, maximum, parameterName),
            ClampToBounds(value.Y, minimum, maximum, parameterName));
    }

    public static GuiVector4Value ClampVector4ToBounds(
        GuiVector4Value value,
        double? minimum,
        double? maximum,
        string parameterName)
    {
        return new GuiVector4Value(
            ClampToBounds(value.X, minimum, maximum, parameterName),
            ClampToBounds(value.Y, minimum, maximum, parameterName),
            ClampToBounds(value.Z, minimum, maximum, parameterName),
            ClampToBounds(value.W, minimum, maximum, parameterName));
    }
}
