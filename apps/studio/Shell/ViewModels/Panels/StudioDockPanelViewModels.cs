using System;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.ViewModels.Panels;

internal abstract class StudioDockPanelViewModel
{
    protected StudioDockPanelViewModel(StudioShellViewModel shell)
    {
        ArgumentNullException.ThrowIfNull(shell);
        Shell = shell;
    }

    public StudioShellViewModel Shell { get; }
}

internal sealed class StudioHierarchyPanelViewModel(StudioShellViewModel shell)
    : StudioDockPanelViewModel(shell);

internal sealed class StudioScenePanelViewModel(StudioShellViewModel shell)
    : StudioDockPanelViewModel(shell);

internal sealed class StudioInspectorPanelViewModel(StudioShellViewModel shell)
    : StudioDockPanelViewModel(shell);

internal sealed class StudioProjectPanelViewModel(StudioShellViewModel shell)
    : StudioDockPanelViewModel(shell);
