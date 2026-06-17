namespace Editor.Shell.ViewModels;

public sealed class EditorDockFloatingWindowViewModel : ViewModelBase
{
    public EditorDockFloatingWindowViewModel(EditorDockWorkspaceViewModel dockWorkspace)
    {
        DockWorkspace = dockWorkspace;
    }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }
}
