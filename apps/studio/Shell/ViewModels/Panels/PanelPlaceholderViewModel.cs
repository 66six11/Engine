using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Panels;

public sealed class PanelPlaceholderViewModel : ViewModelBase
{
    public PanelPlaceholderViewModel(string title)
    {
        Title = title;
    }

    public string Title { get; }
}
