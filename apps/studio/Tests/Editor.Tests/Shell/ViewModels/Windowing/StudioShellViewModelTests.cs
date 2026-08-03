using System;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Windowing;

public sealed class StudioShellViewModelTests
{
    [Fact]
    public void Starting_transitions_once_to_real_empty_workspace()
    {
        using var viewModel = new StudioShellViewModel();

        Assert.Equal(StudioShellStage.Starting, viewModel.Stage);
        Assert.True(viewModel.IsStarting);
        Assert.False(viewModel.IsWorkspaceEmpty);
        Assert.Equal("Starting", viewModel.StartingStateText);

        viewModel.MarkReady();

        Assert.Equal(StudioShellStage.Ready, viewModel.Stage);
        Assert.False(viewModel.IsStarting);
        Assert.True(viewModel.IsWorkspaceEmpty);
        Assert.Equal("No Project", viewModel.ProjectStateText);
        Assert.Equal("No Document", viewModel.DocumentStateText);
    }

    [Fact]
    public void Ready_cannot_be_reentered_or_reached_after_shutdown_begins()
    {
        using var ready = new StudioShellViewModel();
        ready.MarkReady();
        Assert.Throws<InvalidOperationException>(ready.MarkReady);

        using var stopping = new StudioShellViewModel();
        stopping.MarkStopping();
        Assert.Equal(StudioShellStage.Stopping, stopping.Stage);
        Assert.Throws<InvalidOperationException>(stopping.MarkReady);
    }

    [Fact]
    public void Disposed_shell_rejects_late_completion()
    {
        var viewModel = new StudioShellViewModel();
        viewModel.Dispose();

        Assert.Throws<ObjectDisposedException>(viewModel.MarkReady);
        Assert.Throws<ObjectDisposedException>(viewModel.MarkStopping);
    }
}
