using System;

namespace Asharia.Editor.Extensions;

public readonly record struct EditorAssemblyName
{
    private readonly string? value_;

    private EditorAssemblyName(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static EditorAssemblyName Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Editor assembly name must be a canonical managed simple name.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out EditorAssemblyName result)
    {
        if (EditorIdentityValidation.IsManagedAssemblyName(value))
        {
            result = new EditorAssemblyName(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
