using System;
using System.Linq;
using Editor.Core.Models;
using Editor.Shell.ViewModels;
using Xunit;

namespace Editor.Tests.Shell.ViewModels;

public sealed class CommandPaletteViewModelTests
{
    [Fact]
    public void Open_resets_query_and_selects_first_action()
    {
        var viewModel = CreatePalette();
        viewModel.Query = "console";

        viewModel.OpenCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
        Assert.Equal(string.Empty, viewModel.Query);
        Assert.Equal(["Scene View", "Console"], viewModel.FilteredItems.Select(item => item.Title));
        Assert.Equal("Scene View", viewModel.SelectedItem?.Title);
    }

    [Fact]
    public void Query_filters_by_title_menu_path_or_id()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        viewModel.Query = "window/panels/console";

        var item = Assert.Single(viewModel.FilteredItems);
        Assert.Equal("Console", item.Title);

        viewModel.Query = "workbench.panel.scene";

        item = Assert.Single(viewModel.FilteredItems);
        Assert.Equal("Scene View", item.Title);
    }

    [Fact]
    public void Query_reports_no_matches()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        viewModel.Query = "missing";

        Assert.Empty(viewModel.FilteredItems);
        Assert.True(viewModel.HasNoMatches);
        Assert.Null(viewModel.SelectedItem);
    }

    [Fact]
    public void ExecuteSelected_runs_action_and_closes_on_success()
    {
        string? executedActionId = null;
        var viewModel = CreatePalette(action =>
        {
            executedActionId = action.Id;
            return true;
        });
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.Equal("workbench.panel.console", executedActionId);
        Assert.False(viewModel.IsOpen);
    }

    [Fact]
    public void ExecuteSelected_keeps_palette_open_when_action_fails()
    {
        var viewModel = CreatePalette(_ => false);
        viewModel.OpenCommand.Execute(null);

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
    }

    private static CommandPaletteViewModel CreatePalette(
        Func<WorkbenchActionDescriptor, bool>? execute = null)
    {
        return new CommandPaletteViewModel(
            [
                new WorkbenchActionDescriptor(
                    "workbench.panel.scene-view",
                    "Scene View",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Scene View",
                    TargetId: "scene-view"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.console",
                    "Console",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Console",
                    TargetId: "console"),
            ],
            execute ?? (_ => true));
    }
}
