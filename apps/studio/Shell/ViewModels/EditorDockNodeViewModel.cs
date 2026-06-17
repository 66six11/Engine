namespace Editor.Shell.ViewModels;

public abstract class EditorDockNodeViewModel : ViewModelBase
{
    protected EditorDockNodeViewModel(string id)
    {
        Id = id;
    }

    public string Id { get; }
}
