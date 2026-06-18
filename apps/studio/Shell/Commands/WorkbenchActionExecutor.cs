using System;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

internal interface IWorkbenchActionExecutor
{
    bool Execute(WorkbenchActionDescriptor action);
}

internal sealed class WorkbenchActionExecutor : IWorkbenchActionExecutor
{
    private readonly PanelCommandService panelCommandService_;

    public WorkbenchActionExecutor(PanelCommandService panelCommandService)
    {
        ArgumentNullException.ThrowIfNull(panelCommandService);

        panelCommandService_ = panelCommandService;
    }

    public bool Execute(WorkbenchActionDescriptor action)
    {
        ArgumentNullException.ThrowIfNull(action);

        return action.Kind switch
        {
            WorkbenchActionKind.OpenPanel => panelCommandService_.OpenOrFocusPanel(action.TargetId),
            _ => false,
        };
    }
}
