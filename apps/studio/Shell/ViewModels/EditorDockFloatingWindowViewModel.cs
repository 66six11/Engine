namespace Editor.Shell.ViewModels;

public sealed class EditorDockFloatingWindowViewModel : ViewModelBase
{
    public EditorDockFloatingWindowViewModel(string title, EditorDockWorkspaceViewModel dockWorkspace)
    {
        Title = title;
        DockWorkspace = dockWorkspace;
    }

    public string Title { get; }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }
}
