using System;
using System.Collections.Generic;
using System.Linq;

namespace Editor.Core.Models.Dialogs;

public sealed record EditorDialogRequest
{
    public EditorDialogRequest(
        EditorDialogKind kind,
        string title,
        string message,
        bool isCancelable,
        IReadOnlyList<EditorDialogButtonDescriptor> Buttons)
    {
        if (string.IsNullOrWhiteSpace(title))
        {
            throw new ArgumentException("Dialog title must not be empty.", nameof(title));
        }

        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException("Dialog message must not be empty.", nameof(message));
        }

        ArgumentNullException.ThrowIfNull(Buttons);
        if (Buttons.Count == 0)
        {
            throw new ArgumentException("Dialog requests require at least one button.", nameof(Buttons));
        }

        Kind = kind;
        Title = title;
        Message = message;
        IsCancelable = isCancelable;
        this.Buttons = Buttons.ToArray();
    }

    public EditorDialogKind Kind { get; }

    public string Title { get; }

    public string Message { get; }

    public bool IsCancelable { get; }

    public IReadOnlyList<EditorDialogButtonDescriptor> Buttons { get; }

    public static EditorDialogRequest Information(string title, string message)
    {
        return new EditorDialogRequest(
            EditorDialogKind.Information,
            title,
            message,
            isCancelable: true,
            Buttons:
            [
                new EditorDialogButtonDescriptor("ok", "OK", EditorDialogButtonRole.Accept, IsDefault: true),
            ]);
    }

    public static EditorDialogRequest Confirmation(
        string title,
        string message,
        string acceptText,
        string rejectText)
    {
        return new EditorDialogRequest(
            EditorDialogKind.Confirmation,
            title,
            message,
            isCancelable: true,
            Buttons:
            [
                new EditorDialogButtonDescriptor("accept", acceptText, EditorDialogButtonRole.Accept, IsDefault: true),
                new EditorDialogButtonDescriptor("reject", rejectText, EditorDialogButtonRole.Reject),
            ]);
    }
}
