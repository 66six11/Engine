using Editor.Features.Project.ViewModels;
using Xunit;

namespace Editor.Tests.Features.Project;

public sealed class ProjectPanelViewModelTests
{
    [Fact]
    public void Project_panel_describes_only_the_inactive_asset_workspace()
    {
        var viewModel = new ProjectPanelViewModel();

        Assert.Equal("No active project", viewModel.ProjectDisplayName);
        Assert.Equal("Project assets unavailable", viewModel.StateTitle);
        Assert.Contains("folders and assets", viewModel.StateMessage);
        Assert.Contains("activate a project", viewModel.UnavailableReason);
        Assert.False(viewModel.CanSearch);
    }
}
