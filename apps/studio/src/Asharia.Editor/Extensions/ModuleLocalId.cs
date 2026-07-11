using System;

namespace Asharia.Editor.Extensions;

public readonly record struct ModuleLocalId
{
    private readonly string? value_;

    private ModuleLocalId(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static ModuleLocalId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Module local id must be a lowercase dot-separated namespace.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out ModuleLocalId result)
    {
        if (EditorIdentityValidation.IsLowercaseNamespacedId(value, allowColon: false))
        {
            result = new ModuleLocalId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
