using System;
using System.Collections.Generic;
using Asharia.Editor.Commands;
using Avalonia.Input;
using Editor.Core.Models.Workbench;

namespace Editor.Shell.Commands;

internal sealed class WorkbenchShortcutRouter
{
    private readonly IReadOnlyList<WorkbenchShortcutBinding> bindings_;
    private readonly IWorkbenchCommandRouter commandRouter_;

    private WorkbenchShortcutRouter(
        IReadOnlyList<WorkbenchShortcutBinding> bindings,
        IWorkbenchCommandRouter commandRouter)
    {
        bindings_ = bindings;
        commandRouter_ = commandRouter;
    }

    public static WorkbenchShortcutRouter FromActions(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        IWorkbenchCommandRouter commandRouter)
    {
        ArgumentNullException.ThrowIfNull(actions);
        ArgumentNullException.ThrowIfNull(commandRouter);

        var bindings = new List<WorkbenchShortcutBinding>();
        foreach (var action in actions)
        {
            if (WorkbenchShortcutGesture.TryParse(action.DefaultShortcut, out var gesture))
            {
                bindings.Add(new WorkbenchShortcutBinding(action.Id, gesture));
            }
        }

        return new WorkbenchShortcutRouter(bindings, commandRouter);
    }

    public EditorCommandExecutionResult? TryExecute(
        Key key,
        KeyModifiers modifiers,
        bool isTextInputFocused)
    {
        foreach (var binding in bindings_)
        {
            if (!binding.Gesture.Matches(key, modifiers))
            {
                continue;
            }

            if (isTextInputFocused && binding.Gesture.Modifiers == KeyModifiers.None)
            {
                return null;
            }

            return commandRouter_.Execute(binding.CommandId);
        }

        return null;
    }

    private sealed record WorkbenchShortcutBinding(
        string CommandId,
        WorkbenchShortcutGesture Gesture);
}
