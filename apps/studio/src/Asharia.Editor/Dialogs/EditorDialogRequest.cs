using System;
using System.Collections.Generic;
using System.Linq;

namespace Asharia.Editor.Dialogs;

public sealed class EditorDialogRequest
{
    public EditorDialogRequest(
        EditorDialogSeverity severity,
        string? title,
        string message,
        bool allowSystemDismiss,
        IReadOnlyList<EditorDialogActionDescriptor> actions)
    {
        if (!Enum.IsDefined(severity))
        {
            throw new ArgumentOutOfRangeException(
                nameof(severity),
                severity,
                "Dialog severity is invalid.");
        }

        if (title is not null && string.IsNullOrWhiteSpace(title))
        {
            throw new ArgumentException("Dialog title must be null or non-empty.", nameof(title));
        }

        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException("Dialog message must not be empty.", nameof(message));
        }

        ArgumentNullException.ThrowIfNull(actions);
        if (actions.Count is < 1 or > 3)
        {
            throw new ArgumentException("Dialog requests require one to three actions.", nameof(actions));
        }

        var copied = actions.ToArray();
        if (copied.Any(static action => action is null))
        {
            throw new ArgumentException("Dialog actions must not contain null.", nameof(actions));
        }

        if (copied.Select(static action => action.Id).Distinct().Count() != copied.Length)
        {
            throw new ArgumentException("Dialog action identities must be unique.", nameof(actions));
        }

        if (copied.Select(static action => action.Role).Distinct().Count() != copied.Length)
        {
            throw new ArgumentException("Dialog action roles must be unique.", nameof(actions));
        }

        if (copied.Count(static action => action.Role == EditorDialogActionRole.Dismiss) != 1)
        {
            throw new ArgumentException("Dialog requests require exactly one dismiss action.", nameof(actions));
        }

        if (copied.Count(static action => action.IsDefault) > 1)
        {
            throw new ArgumentException("Dialog requests allow at most one default action.", nameof(actions));
        }

        if (copied.Count(static action => action.IsDestructive) > 1)
        {
            throw new ArgumentException("Dialog requests allow at most one destructive action.", nameof(actions));
        }

        if (copied.Count(static action => action.IsDefault || action.IsDestructive) > 1)
        {
            throw new ArgumentException(
                "Dialog requests allow at most one emphasized action.",
                nameof(actions));
        }

        if (copied.Any(static action =>
            action.Role == EditorDialogActionRole.Dismiss && action.IsDestructive))
        {
            throw new ArgumentException("A dismiss action cannot be destructive.", nameof(actions));
        }

        Severity = severity;
        Title = title;
        Message = message;
        AllowSystemDismiss = allowSystemDismiss;
        Actions = Array.AsReadOnly(copied);
    }

    public EditorDialogSeverity Severity { get; }

    public string? Title { get; }

    public string Message { get; }

    public bool AllowSystemDismiss { get; }

    public IReadOnlyList<EditorDialogActionDescriptor> Actions { get; }
}
