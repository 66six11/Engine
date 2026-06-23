using System;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

internal sealed class WorkbenchCommandStatusMessageRouter : IWorkbenchCommandRouter
{
    private readonly IWorkbenchCommandRouter inner_;
    private readonly Action<EditorStatusMessageSnapshot> publishStatusMessage_;

    public WorkbenchCommandStatusMessageRouter(
        IWorkbenchCommandRouter inner,
        Action<EditorStatusMessageSnapshot> publishStatusMessage)
    {
        ArgumentNullException.ThrowIfNull(inner);
        ArgumentNullException.ThrowIfNull(publishStatusMessage);

        inner_ = inner;
        publishStatusMessage_ = publishStatusMessage;
    }

    public WorkbenchCommandExecutionResult Execute(string commandId)
    {
        var result = inner_.Execute(commandId);
        publishStatusMessage_(EditorStatusMessageSnapshot.FromCommandResult(result));
        return result;
    }
}
