using System;
using Asharia.Editor.Extensions;

namespace Asharia.Editor.Contributions;

public readonly record struct EditorContributionId
{
    private readonly string? value_;

    private EditorContributionId(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static EditorContributionId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Editor contribution id must be a lowercase dot-separated namespace.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out EditorContributionId result)
    {
        if (EditorIdentityValidation.IsLowercaseNamespacedId(value, allowColon: false))
        {
            result = new EditorContributionId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
