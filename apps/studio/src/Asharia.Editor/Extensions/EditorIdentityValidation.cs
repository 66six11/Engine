using System;

namespace Asharia.Editor.Extensions;

internal static class EditorIdentityValidation
{
    public static bool IsLowercaseKebabId(string? value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return false;
        }

        var atSegmentStart = true;
        for (var index = 0; index < value.Length; index++)
        {
            var character = value[index];
            if (IsLowercaseAsciiLetter(character) || char.IsAsciiDigit(character))
            {
                atSegmentStart = false;
                continue;
            }

            if (character != '-' || atSegmentStart)
            {
                return false;
            }

            atSegmentStart = true;
        }

        return !atSegmentStart;
    }

    public static bool IsLowercaseNamespacedId(string? value, bool allowColon)
    {
        if (string.IsNullOrEmpty(value))
        {
            return false;
        }

        var hasNamespaceSeparator = false;
        var atSegmentStart = true;

        for (var index = 0; index < value.Length; index++)
        {
            var character = value[index];
            if (IsLowercaseAsciiLetter(character) || char.IsAsciiDigit(character))
            {
                atSegmentStart = false;
                continue;
            }

            if (character == '-')
            {
                if (atSegmentStart
                    || index + 1 >= value.Length
                    || !(IsLowercaseAsciiLetter(value[index + 1]) || char.IsAsciiDigit(value[index + 1])))
                {
                    return false;
                }

                continue;
            }

            if (character == '.' || (allowColon && character == ':'))
            {
                if (atSegmentStart)
                {
                    return false;
                }

                hasNamespaceSeparator = true;
                atSegmentStart = true;
                continue;
            }

            return false;
        }

        return hasNamespaceSeparator && !atSegmentStart;
    }

    public static bool IsManagedAssemblyName(string? value)
    {
        if (string.IsNullOrEmpty(value)
            || !(IsAsciiLetter(value[0]) || value[0] == '_'))
        {
            return false;
        }

        var previousWasDot = false;
        for (var index = 1; index < value.Length; index++)
        {
            var character = value[index];
            if (character == '.')
            {
                if (previousWasDot || index + 1 >= value.Length)
                {
                    return false;
                }

                previousWasDot = true;
                continue;
            }

            if (!(IsAsciiLetter(character)
                    || char.IsAsciiDigit(character)
                    || character is '_' or '-'))
            {
                return false;
            }

            previousWasDot = false;
        }

        return value[^1] is not '-' and not '.';
    }

    public static bool IsVersionedCapabilityId(string? value)
    {
        if (!IsLowercaseNamespacedId(value, allowColon: false))
        {
            return false;
        }

        var versionSeparator = value!.LastIndexOf('.');
        var version = value.AsSpan(versionSeparator + 1);
        if (version.Length < 2 || version[0] != 'v' || version[1] == '0')
        {
            return false;
        }

        for (var index = 1; index < version.Length; index++)
        {
            if (!char.IsAsciiDigit(version[index]))
            {
                return false;
            }
        }

        return true;
    }

    private static bool IsAsciiLetter(char value)
    {
        return IsLowercaseAsciiLetter(value) || value is >= 'A' and <= 'Z';
    }

    private static bool IsLowercaseAsciiLetter(char value)
    {
        return value is >= 'a' and <= 'z';
    }
}
