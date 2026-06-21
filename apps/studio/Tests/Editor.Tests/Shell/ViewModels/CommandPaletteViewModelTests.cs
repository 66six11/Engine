using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Models;
using Editor.Shell.ViewModels;
using Xunit;

namespace Editor.Tests.Shell.ViewModels;

public sealed class CommandPaletteViewModelTests
{
    [Fact]
    public void Open_resets_query_groups_actions_and_selects_first_command()
    {
        var viewModel = CreatePalette();
        viewModel.Query = "console";

        viewModel.OpenCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
        Assert.Equal(string.Empty, viewModel.Query);
        Assert.Equal(
            ["Window", "Scene View", "Console", "Disabled", "Tools", "Command Palette"],
            viewModel.FilteredItems.Select(item => item.Title));
        Assert.Equal([true, false, false, false, true, false], viewModel.FilteredItems.Select(item => item.IsHeader));
        Assert.Equal("Scene View", viewModel.SelectedItem?.Title);
        Assert.True(viewModel.SelectedItem?.IsCommand);
    }

    [Fact]
    public void Item_exposes_command_metadata()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        var item = viewModel.FilteredItems.Single(item => item.Id == "workbench.panel.console");

        Assert.Equal("Window", item.Category);
        Assert.Equal("Ctrl+Alt+C", item.DefaultShortcut);
        Assert.True(item.HasDefaultShortcut);
        Assert.True(item.IsEnabled);
        Assert.False(item.HasDisabledReason);
        Assert.Equal(1.0, item.RowOpacity);
    }

    [Fact]
    public void Disabled_item_exposes_reason_and_dimmed_opacity()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        var item = viewModel.FilteredItems.Single(item => item.Id == "workbench.panel.disabled");

        Assert.False(item.IsEnabled);
        Assert.Equal("Disabled by test", item.DisabledReason);
        Assert.True(item.HasDisabledReason);
        Assert.Equal(0.55, item.RowOpacity);
    }

    [Fact]
    public void Query_filters_by_title_menu_path_id_category_or_search_text()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        viewModel.Query = "window/panels/console";

        var item = Assert.Single(CommandRows(viewModel));
        Assert.Equal("Console", item.Title);

        viewModel.Query = "workbench.panel.scene";

        item = Assert.Single(CommandRows(viewModel));
        Assert.Equal("Scene View", item.Title);

        viewModel.Query = "diagnostics";

        item = Assert.Single(CommandRows(viewModel));
        Assert.Equal("Console", item.Title);

        viewModel.Query = "window";

        Assert.Equal(["Scene View", "Console", "Disabled"], CommandRows(viewModel).Select(command => command.Title));
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
    public void ExecuteSelected_runs_command_by_id_and_closes_on_success()
    {
        string? executedCommandId = null;
        var viewModel = CreatePalette(commandId =>
        {
            executedCommandId = commandId;
            return WorkbenchCommandExecutionResult.Success(commandId);
        });
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.Equal("workbench.panel.console", executedCommandId);
        Assert.False(viewModel.IsOpen);
    }

    [Fact]
    public void ExecuteSelected_keeps_palette_open_when_command_fails()
    {
        var viewModel = CreatePalette(commandId => WorkbenchCommandExecutionResult.Failed(commandId, "Failed by test"));
        viewModel.OpenCommand.Execute(null);

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
    }

    [Fact]
    public void ExecuteSelected_ignores_disabled_action()
    {
        string? executedCommandId = null;
        var viewModel = CreatePalette(commandId =>
        {
            executedCommandId = commandId;
            return WorkbenchCommandExecutionResult.Success(commandId);
        });
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "disabled";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.Null(executedCommandId);
        Assert.True(viewModel.IsOpen);
    }

    [Fact]
    public void Category_header_rows_are_not_executable()
    {
        string? executedCommandId = null;
        var viewModel = CreatePalette(commandId =>
        {
            executedCommandId = commandId;
            return WorkbenchCommandExecutionResult.Success(commandId);
        });
        viewModel.OpenCommand.Execute(null);
        viewModel.SelectedItem = viewModel.FilteredItems.First(item => item.IsHeader);

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.Null(executedCommandId);
        Assert.True(viewModel.IsOpen);
    }

    private static IReadOnlyList<CommandPaletteItemViewModel> CommandRows(CommandPaletteViewModel viewModel)
    {
        return viewModel.FilteredItems.Where(item => item.IsCommand).ToArray();
    }

    private static CommandPaletteViewModel CreatePalette(
        Func<string, WorkbenchCommandExecutionResult>? execute = null)
    {
        return new CommandPaletteViewModel(
            [
                new WorkbenchActionDescriptor(
                    "workbench.panel.scene-view",
                    "Scene View",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Scene View",
                    TargetId: "scene-view",
                    Category: "Window"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.console",
                    "Console",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Console",
                    TargetId: "console",
                    Category: "Window",
                    DefaultShortcut: "Ctrl+Alt+C",
                    SearchText: "log output diagnostics"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.disabled",
                    "Disabled",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Disabled",
                    TargetId: "disabled",
                    Category: "Window",
                    IsEnabled: false,
                    DisabledReason: "Disabled by test"),
                new WorkbenchActionDescriptor(
                    "workbench.commandPalette.open",
                    "Command Palette",
                    WorkbenchActionKind.OpenCommandPalette,
                    "Tools/Command Palette",
                    Category: "Tools",
                    DefaultShortcut: "Ctrl+Shift+P",
                    SearchText: "quick command launcher"),
            ],
            execute ?? (commandId => WorkbenchCommandExecutionResult.Success(commandId)));
    }
}
