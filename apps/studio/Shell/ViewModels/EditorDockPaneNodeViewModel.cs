namespace Editor.Shell.ViewModels;

public sealed class EditorDockPaneNodeViewModel : EditorDockNodeViewModel
{
    public EditorDockPaneNodeViewModel(string id, EditorDockPaneViewModel pane)
        : base(id)
    {
        Pane = pane;
    }

    public EditorDockPaneViewModel Pane { get; }
}
