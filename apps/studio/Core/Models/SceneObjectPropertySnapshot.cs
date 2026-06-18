using System;

namespace Editor.Core.Models;

public sealed record SceneObjectPropertySnapshot
{
    public SceneObjectPropertySnapshot(
        string id,
        string displayName,
        string value,
        SceneObjectPropertyValueKind valueKind = SceneObjectPropertyValueKind.Text,
        string? diagnostic = null)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            throw new ArgumentException("Scene object property id cannot be null or whitespace.", nameof(id));
        }

        ArgumentNullException.ThrowIfNull(value);

        Id = id;
        DisplayName = string.IsNullOrWhiteSpace(displayName) ? id : displayName;
        Value = value;
        ValueKind = valueKind;
        Diagnostic = string.IsNullOrWhiteSpace(diagnostic) ? null : diagnostic;
    }

    public string Id { get; }

    public string DisplayName { get; }

    public string Value { get; }

    public SceneObjectPropertyValueKind ValueKind { get; }

    public string? Diagnostic { get; }
}
