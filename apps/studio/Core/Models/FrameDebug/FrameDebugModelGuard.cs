using System;
using System.Collections.Generic;

namespace Editor.Core.Models.FrameDebug;

internal static class FrameDebugModelGuard
{
    public static string Require(string value, string parameterName, string description)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException($"{description} cannot be null or whitespace.", parameterName);
        }

        return value;
    }

    public static string DisplayOrId(string value, string id)
    {
        return string.IsNullOrWhiteSpace(value) ? id : value;
    }

    public static IReadOnlyList<T> Copy<T>(IReadOnlyList<T>? values)
    {
        return Array.AsReadOnly(values is null ? Array.Empty<T>() : [.. values]);
    }
}
