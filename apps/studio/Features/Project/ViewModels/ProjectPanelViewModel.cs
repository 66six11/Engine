using Editor.UI.ViewModels;

namespace Editor.Features.Project.ViewModels;

public sealed class ProjectPanelViewModel : ViewModelBase
{
    public string ProjectDisplayName => "No project";

    public string EmptyStateTitle => "No project is open";

    public string EmptyStateMessage =>
        "Open a project to browse its assets and generated content.";

    public bool CanSearch => false;

    public bool CanOpenProject => false;

    public string UnavailableReason =>
        "Project browsing is unavailable until the project service is connected.";
}
