using System;

namespace Asharia.Editor.Extensions;

public readonly record struct PackageName
{
    private readonly string? value_;

    private PackageName(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static PackageName Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Package name must be a lowercase ASCII namespace.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out PackageName result)
    {
        if (EditorIdentityValidation.IsLowercaseNamespacedId(value, allowColon: true))
        {
            result = new PackageName(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
