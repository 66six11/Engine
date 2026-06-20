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
    private readonly Func<bool>? openCommandPalette_;
    private readonly Func<bool>? openAboutDialog_;

    public WorkbenchActionExecutor(
        PanelCommandService panelCommandService,
        Func<bool>? openCommandPalette = null,
        Func<bool>? openAboutDialog = null)
    {
        ArgumentNullException.ThrowIfNull(panelCommandService);

        panelCommandService_ = panelCommandService;
        openCommandPalette_ = openCommandPalette;
        openAboutDialog_ = openAboutDialog;
    }

    public bool Execute(WorkbenchActionDescriptor action)
    {
        ArgumentNullException.ThrowIfNull(action);

        if (!action.IsEnabled)
        {
            return false;
        }

        return action.Kind switch
        {
            WorkbenchActionKind.OpenPanel => panelCommandService_.OpenOrFocusPanel(action.TargetId),
            WorkbenchActionKind.OpenCommandPalette => openCommandPalette_?.Invoke() ?? false,
            WorkbenchActionKind.OpenAboutDialog => openAboutDialog_?.Invoke() ?? false,
            _ => false,
        };
    }
}
