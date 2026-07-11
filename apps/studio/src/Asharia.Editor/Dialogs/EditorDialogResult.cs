using System;

namespace Asharia.Editor.Dialogs;

public sealed record EditorDialogResult
{
    private EditorDialogResult(
        EditorDialogCompletionKind completion,
        EditorDialogActionId? actionId)
    {
        Completion = completion;
        ActionId = actionId;
    }

    public EditorDialogCompletionKind Completion { get; }

    public EditorDialogActionId? ActionId { get; }

    public static EditorDialogResult ActionInvoked(EditorDialogActionId actionId)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Dialog action identity is invalid.", nameof(actionId));
        }

        return new EditorDialogResult(EditorDialogCompletionKind.ActionInvoked, actionId);
    }

    public static EditorDialogResult SystemDismissed()
    {
        return new EditorDialogResult(EditorDialogCompletionKind.SystemDismissed, null);
    }
}
