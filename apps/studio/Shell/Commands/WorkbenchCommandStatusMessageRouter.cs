using System;
using Asharia.Editor.Commands;

namespace Editor.Shell.Commands;

internal sealed class WorkbenchCommandStatusMessageRouter : IWorkbenchCommandRouter
{
    private readonly IWorkbenchCommandRouter inner_;
    private readonly Action<EditorCommandExecutionResult> publishResult_;

    public WorkbenchCommandStatusMessageRouter(
        IWorkbenchCommandRouter inner,
        Action<EditorCommandExecutionResult> publishResult)
    {
        ArgumentNullException.ThrowIfNull(inner);
        ArgumentNullException.ThrowIfNull(publishResult);

        inner_ = inner;
        publishResult_ = publishResult;
    }

    public EditorCommandExecutionResult Execute(string commandId)
    {
        var result = inner_.Execute(commandId);
        publishResult_(result);
        return result;
    }
}
