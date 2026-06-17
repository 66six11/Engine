namespace Editor.Shell.ViewModels;

public sealed class PanelPlaceholderViewModel : ViewModelBase
{
    public PanelPlaceholderViewModel(string title)
    {
        Title = title;
    }

    public string Title { get; }
}
