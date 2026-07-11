using System;

namespace Asharia.Editor.Contributions;

public readonly record struct UiBackendId
{
    private readonly string? value_;

    private UiBackendId(string value)
    {
        value_ = value;
    }

    public static UiBackendId CodeFirst { get; } = new("code-first");

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static UiBackendId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "UI backend id must be a lowercase kebab id.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out UiBackendId result)
    {
        if (IsLowercaseKebabId(value))
        {
            result = new UiBackendId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;

    private static bool IsLowercaseKebabId(string? value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return false;
        }

        var atSegmentStart = true;
        for (var index = 0; index < value.Length; index++)
        {
            var character = value[index];
            if (character is >= 'a' and <= 'z' || char.IsAsciiDigit(character))
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
}
