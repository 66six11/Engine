using System;
using Editor.Core.Models.Workbench;

namespace Editor.Shell.Commands;

internal interface IWorkbenchActionExecutor
{
    bool Execute(WorkbenchActionDescriptor action);
}

internal sealed class WorkbenchActionExecutor : IWorkbenchActionExecutor
{
    private readonly WorkbenchCommandHandlerRegistry commandHandlers_;

    public WorkbenchActionExecutor(WorkbenchCommandHandlerRegistry commandHandlers)
    {
        ArgumentNullException.ThrowIfNull(commandHandlers);

        commandHandlers_ = commandHandlers;
    }

    public bool Execute(WorkbenchActionDescriptor action)
    {
        ArgumentNullException.ThrowIfNull(action);

        if (!action.IsEnabled)
        {
            return false;
        }

        return commandHandlers_.TryExecute(action, out var completed)
            && completed;
    }
}
