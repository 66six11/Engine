using Editor.Features.Project.ViewModels;
using Xunit;

namespace Editor.Tests.Features.Project;

public sealed class ProjectPanelViewModelTests
{
    [Fact]
    public void Empty_project_state_is_explicit_and_unavailable_actions_have_a_reason()
    {
        var viewModel = new ProjectPanelViewModel();

        Assert.Equal("No project", viewModel.ProjectDisplayName);
        Assert.Equal("No project is open", viewModel.EmptyStateTitle);
        Assert.False(viewModel.CanSearch);
        Assert.False(viewModel.CanOpenProject);
        Assert.False(string.IsNullOrWhiteSpace(viewModel.UnavailableReason));
    }
}
