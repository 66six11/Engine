using System;
using Asharia.Editor.Extensions;

namespace Asharia.Editor.Dialogs;

public readonly record struct EditorDialogActionId
{
    private readonly string? value_;

    private EditorDialogActionId(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static EditorDialogActionId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Dialog action id must be a lowercase kebab id.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out EditorDialogActionId result)
    {
        if (EditorIdentityValidation.IsLowercaseKebabId(value))
        {
            result = new EditorDialogActionId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}
