using System;
using Asharia.Editor.Commands;
using Asharia.Editor.UI.CodeFirst.Abstractions;
using Editor.Core.Abstractions;
using Editor.Core.Models.Workbench;

namespace Editor.Shell.Commands;

internal sealed class WorkbenchCommandRouter : IEditorGuiCommandExecutor
{
    private readonly IWorkbenchActionRegistry actionRegistry_;
    private readonly IWorkbenchActionExecutor actionExecutor_;

    public WorkbenchCommandRouter(
        IWorkbenchActionRegistry actionRegistry,
        IWorkbenchActionExecutor actionExecutor)
    {
        ArgumentNullException.ThrowIfNull(actionRegistry);
        ArgumentNullException.ThrowIfNull(actionExecutor);

        actionRegistry_ = actionRegistry;
        actionExecutor_ = actionExecutor;
    }

    public EditorCommandExecutionResult Execute(string commandId)
    {
        ArgumentNullException.ThrowIfNull(commandId);

        var action = actionRegistry_.FindById(commandId);
        if (action is null)
        {
            return EditorCommandExecutionResult.NotFound(commandId);
        }

        if (!action.IsEnabled)
        {
            return EditorCommandExecutionResult.Disabled(
                commandId,
                action.DisabledReason ?? "Command is disabled.");
        }

        try
        {
            return actionExecutor_.Execute(action)
                ? EditorCommandExecutionResult.Success(commandId)
                : EditorCommandExecutionResult.Failed(commandId, $"Command '{commandId}' did not complete.");
        }
        catch (Exception exception)
        {
            return EditorCommandExecutionResult.Failed(commandId, exception.Message);
        }
    }
}
