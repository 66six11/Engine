using System;

namespace Editor.Core.Models.Dialogs;

public sealed record EditorDialogResult(EditorDialogResultKind Kind, string? ButtonId)
{
    public static EditorDialogResult FromButton(EditorDialogButtonDescriptor button)
    {
        ArgumentNullException.ThrowIfNull(button);

        if (button.Role == EditorDialogButtonRole.Accept)
        {
            return new EditorDialogResult(EditorDialogResultKind.Accepted, button.Id);
        }

        if (button.Role == EditorDialogButtonRole.Reject)
        {
            return new EditorDialogResult(EditorDialogResultKind.Rejected, button.Id);
        }

        return new EditorDialogResult(EditorDialogResultKind.Canceled, button.Id);
    }

    public static EditorDialogResult Canceled()
    {
        return new EditorDialogResult(EditorDialogResultKind.Canceled, null);
    }
}
