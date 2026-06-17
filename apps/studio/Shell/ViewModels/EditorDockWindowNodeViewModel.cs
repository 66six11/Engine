namespace Editor.Shell.ViewModels;

public sealed class EditorDockWindowNodeViewModel : EditorDockNodeViewModel
{
    public EditorDockWindowNodeViewModel(string id, EditorDockWindowViewModel window)
        : base(id)
    {
        Window = window;
    }

    public EditorDockWindowViewModel Window { get; }
}
