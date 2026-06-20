using System;

namespace Editor.Core.Models;

public sealed record EditorDialogResult(EditorDialogResultKind Kind, string? ButtonId)
{
    public static EditorDialogResult FromButton(EditorDialogButtonDescriptor button)
    {
        ArgumentNullException.ThrowIfNull(button);

        return button.Role switch
        {
            EditorDialogButtonRole.Accept => new EditorDialogResult(EditorDialogResultKind.Accepted, button.Id),
            EditorDialogButtonRole.Reject => new EditorDialogResult(EditorDialogResultKind.Rejected, button.Id),
            EditorDialogButtonRole.Cancel => new EditorDialogResult(EditorDialogResultKind.Canceled, button.Id),
            _ => new EditorDialogResult(EditorDialogResultKind.Canceled, button.Id),
        };
    }

    public static EditorDialogResult Canceled()
    {
        return new EditorDialogResult(EditorDialogResultKind.Canceled, null);
    }
}
