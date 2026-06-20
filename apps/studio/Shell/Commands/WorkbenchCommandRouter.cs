using System;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

internal interface IWorkbenchCommandRouter
{
    WorkbenchCommandExecutionResult Execute(string commandId);
}

internal sealed class WorkbenchCommandRouter : IWorkbenchCommandRouter
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

    public WorkbenchCommandExecutionResult Execute(string commandId)
    {
        ArgumentNullException.ThrowIfNull(commandId);

        var action = actionRegistry_.FindById(commandId);
        if (action is null)
        {
            return WorkbenchCommandExecutionResult.NotFound(commandId);
        }

        if (!action.IsEnabled)
        {
            return WorkbenchCommandExecutionResult.Disabled(
                commandId,
                action.DisabledReason ?? "Command is disabled.");
        }

        try
        {
            return actionExecutor_.Execute(action)
                ? WorkbenchCommandExecutionResult.Success(commandId)
                : WorkbenchCommandExecutionResult.Failed(commandId, $"Command '{commandId}' did not complete.");
        }
        catch (Exception exception)
        {
            return WorkbenchCommandExecutionResult.Failed(commandId, exception.Message);
        }
    }
}
