using System;

namespace Asharia.Editor.Dialogs;

public sealed record EditorDialogActionDescriptor
{
    public EditorDialogActionDescriptor(
        EditorDialogActionId id,
        string text,
        EditorDialogActionRole role,
        bool isDefault = false,
        bool isDestructive = false)
    {
        if (!id.IsValid)
        {
            throw new ArgumentException("Dialog action identity is invalid.", nameof(id));
        }

        if (string.IsNullOrWhiteSpace(text))
        {
            throw new ArgumentException("Dialog action text must not be empty.", nameof(text));
        }

        if (!Enum.IsDefined(role))
        {
            throw new ArgumentOutOfRangeException(nameof(role), role, "Dialog action role is invalid.");
        }

        Id = id;
        Text = text;
        Role = role;
        IsDefault = isDefault;
        IsDestructive = isDestructive;
    }

    public EditorDialogActionId Id { get; }

    public string Text { get; }

    public EditorDialogActionRole Role { get; }

    public bool IsDefault { get; }

    public bool IsDestructive { get; }
}
