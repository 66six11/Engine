using System;

namespace Editor.Core.Models;

public readonly record struct EditorExtensionId
{
    public EditorExtensionId(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("Editor extension id must not be empty.", nameof(value));
        }

        Value = value;
    }

    public string Value { get; }

    public override string ToString()
    {
        return Value;
    }
}
