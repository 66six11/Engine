using System;

namespace Editor.Core.Models.Viewports;

public readonly record struct ViewportId
{
    public ViewportId(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException(
                "Viewport id must not be blank.",
                nameof(value));
        }

        Value = value.Trim();
    }

    public string Value { get; }

    public bool IsDefault => string.IsNullOrWhiteSpace(Value);

    public static ViewportId NewId()
    {
        return new ViewportId(Guid.NewGuid().ToString("N"));
    }

    public override string ToString()
    {
        return Value ?? string.Empty;
    }
}
