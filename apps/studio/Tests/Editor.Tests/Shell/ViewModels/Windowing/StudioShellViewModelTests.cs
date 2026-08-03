using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.TestSupport;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Windowing;

public sealed class StudioShellViewModelTests
{
    [Fact]
    public void Starting_transitions_once_to_real_empty_workspace()
    {
        using var viewModel = StudioShellTestFactory.Create();

        Assert.Equal(StudioShellStage.Starting, viewModel.Stage);
        Assert.True(viewModel.IsStarting);
        Assert.False(viewModel.IsWorkspaceVisible);
        Assert.Equal("Starting", viewModel.StartingStateText);

        viewModel.MarkReady();

        Assert.Equal(StudioShellStage.Ready, viewModel.Stage);
        Assert.False(viewModel.IsStarting);
        Assert.True(viewModel.IsWorkspaceVisible);
        Assert.Equal("No Project", viewModel.ProjectStateText);
        Assert.Equal("No Document", viewModel.DocumentStateText);
    }

    [Fact]
    public void Ready_cannot_be_reentered_or_reached_after_shutdown_begins()
    {
        using var ready = StudioShellTestFactory.Create();
        ready.MarkReady();
        Assert.Throws<InvalidOperationException>(ready.MarkReady);

        using var stopping = StudioShellTestFactory.Create();
        stopping.MarkStopping();
        Assert.Equal(StudioShellStage.Stopping, stopping.Stage);
        Assert.Throws<InvalidOperationException>(stopping.MarkReady);
    }

    [Fact]
    public void Disposed_shell_rejects_late_completion()
    {
        var viewModel = StudioShellTestFactory.Create();
        viewModel.Dispose();

        Assert.Throws<ObjectDisposedException>(viewModel.MarkReady);
        Assert.Throws<ObjectDisposedException>(viewModel.MarkStopping);
    }

    [Fact]
    public async Task Create_command_uses_the_selected_parent_and_projects_ready_state()
    {
        using var viewModel = StudioShellTestFactory.Create(
            out var projectSession,
            out var dialogs);
        viewModel.MarkReady();
        viewModel.NewProjectName = "Sample";
        dialogs.ParentDirectory = "C:\\Projects";
        var ready = Ready("Sample", "C:\\Projects\\Sample");
        projectSession.CreateHandler = (parent, name, _) =>
        {
            Assert.Equal("C:\\Projects", parent);
            Assert.Equal("Sample", name);
            projectSession.Publish(ready);
            return ValueTask.FromResult(
                ProjectSessionOperationResult.Success(ready, "Created project 'Sample'."));
        };

        viewModel.CreateProjectCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.True(viewModel.HasProject);
        Assert.False(viewModel.HasNoProject);
        Assert.Equal("Sample", viewModel.ProjectStateText);
        Assert.Equal("C:\\Projects\\Sample", viewModel.ProjectPathText);
        Assert.Equal("Created project 'Sample'.", viewModel.ProjectOperationMessage);
        Assert.Contains("Sample", viewModel.WindowTitle, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Canceled_open_dialog_does_not_call_the_project_session()
    {
        using var viewModel = StudioShellTestFactory.Create(
            out var projectSession,
            out var dialogs);
        viewModel.MarkReady();
        dialogs.ProjectDescriptor = null;
        var called = false;
        projectSession.OpenHandler = (_, _) =>
        {
            called = true;
            throw new InvalidOperationException("Open must not be called.");
        };

        viewModel.OpenProjectCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.False(called);
        Assert.True(viewModel.HasNoProject);
        Assert.Equal(string.Empty, viewModel.ProjectOperationMessage);
    }

    private static ProjectSessionSnapshot Ready(string name, string root) =>
        ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                ProjectSessionId.CreateNew(),
                Guid.NewGuid(),
                name,
                root));

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(1));
        while (!condition())
        {
            await Task.Delay(10, timeout.Token);
        }
    }
}
