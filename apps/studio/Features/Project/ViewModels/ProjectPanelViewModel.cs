using Editor.UI.ViewModels;

namespace Editor.Features.Project.ViewModels;

public sealed class ProjectPanelViewModel : ViewModelBase
{
    public string ProjectDisplayName => "No active project";

    public bool CanSearch => false;

    public string UnavailableReason =>
        "Open and activate a project before browsing project assets.";

    public string StateTitle => "Project assets unavailable";

    public string StateMessage =>
        "This panel will show the active project's folders and assets.";
}
