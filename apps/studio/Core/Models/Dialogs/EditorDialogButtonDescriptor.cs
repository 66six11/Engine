using System;

namespace Editor.Core.Models.Dialogs;

public sealed record EditorDialogButtonDescriptor
{
    public EditorDialogButtonDescriptor(
        string id,
        string text,
        EditorDialogButtonRole role,
        bool IsDefault = false)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            throw new ArgumentException("Dialog button id must not be empty.", nameof(id));
        }

        if (string.IsNullOrWhiteSpace(text))
        {
            throw new ArgumentException("Dialog button text must not be empty.", nameof(text));
        }

        Id = id;
        Text = text;
        Role = role;
        this.IsDefault = IsDefault;
    }

    public string Id { get; }

    public string Text { get; }

    public EditorDialogButtonRole Role { get; }

    public bool IsDefault { get; }
}
