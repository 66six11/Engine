using System;

namespace Asharia.Editor.Extensions;

public readonly record struct ScopeInstanceId
{
    private const string ProjectPrefix = "project:";
    private readonly string? value_;

    private ScopeInstanceId(string value)
    {
        value_ = value;
    }

    public static ScopeInstanceId Application { get; } = new("application");

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static ScopeInstanceId ForProject(Guid projectSessionId)
    {
        if (projectSessionId == Guid.Empty)
        {
            throw new ArgumentException(
                "Project session id must not be empty.",
                nameof(projectSessionId));
        }

        return new ScopeInstanceId(ProjectPrefix + projectSessionId.ToString("D"));
    }

    public static bool TryCreate(string? value, out ScopeInstanceId result)
    {
        if (string.Equals(value, Application.Value, StringComparison.Ordinal))
        {
            result = Application;
            return true;
        }

        if (value is not null
            && value.StartsWith(ProjectPrefix, StringComparison.Ordinal)
            && Guid.TryParseExact(value.AsSpan(ProjectPrefix.Length), "D", out var projectId)
            && projectId != Guid.Empty
            && string.Equals(
                value,
                ProjectPrefix + projectId.ToString("D"),
                StringComparison.Ordinal))
        {
            result = new ScopeInstanceId(value);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
