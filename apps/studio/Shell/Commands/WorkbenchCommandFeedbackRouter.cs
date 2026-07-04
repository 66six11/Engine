using System;
using Editor.Core.Models.Workbench;

namespace Editor.Shell.Commands;

internal sealed class WorkbenchCommandFeedbackRouter : IWorkbenchCommandRouter
{
    private readonly IWorkbenchCommandRouter inner_;
    private readonly Action<WorkbenchCommandExecutionResult> publishResult_;

    public WorkbenchCommandFeedbackRouter(
        IWorkbenchCommandRouter inner,
        Action<WorkbenchCommandExecutionResult> publishResult)
    {
        ArgumentNullException.ThrowIfNull(inner);
        ArgumentNullException.ThrowIfNull(publishResult);

        inner_ = inner;
        publishResult_ = publishResult;
    }

    public WorkbenchCommandExecutionResult Execute(string commandId)
    {
        var result = inner_.Execute(commandId);
        publishResult_(result);
        return result;
    }
}
