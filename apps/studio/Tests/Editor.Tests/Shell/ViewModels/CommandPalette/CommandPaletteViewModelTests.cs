using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Models;
using Editor.Core.Models.Workbench;
using Editor.Shell.ViewModels.CommandPalette;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.CommandPalette;

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
    public void Query_filters_by_default_shortcut()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        viewModel.Query = "ctrl+shift+p";

        var item = Assert.Single(CommandRows(viewModel));
        Assert.Equal("Command Palette", item.Title);
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
    public void Successful_execution_records_recent_command_and_clears_result_message()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";

        viewModel.ExecuteSelectedCommand.Execute(null);
        viewModel.OpenCommand.Execute(null);

        Assert.False(viewModel.HasLastResultMessage);
        Assert.Equal(
            ["Recent", "Console", "Window", "Scene View", "Disabled", "Tools", "Command Palette"],
            viewModel.FilteredItems.Select(item => item.Title));
        Assert.Equal("Console", viewModel.SelectedItem?.Title);
    }

    [Fact]
    public void Recent_promotion_is_disabled_while_query_is_active()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";
        viewModel.ExecuteSelectedCommand.Execute(null);
        viewModel.OpenCommand.Execute(null);

        viewModel.Query = "window";

        Assert.DoesNotContain(viewModel.FilteredItems, item => item.IsHeader && item.Title == "Recent");
        Assert.Equal(["Scene View", "Console", "Disabled"], CommandRows(viewModel).Select(item => item.Title));
    }

    [Fact]
    public void Failed_execution_keeps_palette_open_and_publishes_result_message()
    {
        var viewModel = CreatePalette(commandId => WorkbenchCommandExecutionResult.Failed(commandId, "Failed by test"));
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
        Assert.True(viewModel.HasLastResultMessage);
        Assert.Equal("Failed by test", viewModel.LastResultMessage);
        Assert.DoesNotContain(viewModel.FilteredItems, item => item.IsHeader && item.Title == "Recent");
    }

    [Fact]
    public void Failed_execution_with_blank_message_uses_fallback_result_message()
    {
        var viewModel = CreatePalette(commandId => WorkbenchCommandExecutionResult.Failed(commandId, "   "));
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
        Assert.True(viewModel.HasLastResultMessage);
        Assert.Equal("Command 'workbench.panel.console' did not complete.", viewModel.LastResultMessage);
    }

    [Fact]
    public void Failed_execution_publishes_result_message_property_notifications()
    {
        var changedProperties = new List<string>();
        var viewModel = CreatePalette(commandId => WorkbenchCommandExecutionResult.Failed(commandId, "Failed by test"));
        viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.Contains(nameof(CommandPaletteViewModel.LastResultMessage), changedProperties);
        Assert.Contains(nameof(CommandPaletteViewModel.HasLastResultMessage), changedProperties);
    }

    [Fact]
    public void Recent_commands_are_capped_deduped_and_newest_first()
    {
        var viewModel = CreatePaletteWithCommandCount(6);
        viewModel.OpenCommand.Execute(null);
        foreach (var commandId in new[]
        {
            "test.command-1",
            "test.command-2",
            "test.command-3",
            "test.command-4",
            "test.command-5",
            "test.command-6",
            "test.command-3",
        })
        {
            ExecuteMatchingCommand(viewModel, commandId);
        }

        var recentItems = viewModel.FilteredItems
            .Skip(1)
            .TakeWhile(item => !item.IsHeader)
            .ToArray();

        Assert.Equal("Recent", viewModel.FilteredItems.First().Title);
        Assert.Equal(
            ["Command 3", "Command 6", "Command 5", "Command 4", "Command 2"],
            recentItems.Select(item => item.Title));
        Assert.Equal(recentItems.Length, recentItems.Select(item => item.Id).Distinct(StringComparer.Ordinal).Count());
        Assert.DoesNotContain(recentItems, item => item.Title == "Command 1");
    }

    [Fact]
    public void Not_found_execution_keeps_palette_open_and_publishes_result_message()
    {
        var viewModel = CreatePalette(commandId => WorkbenchCommandExecutionResult.NotFound(commandId));
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
        Assert.True(viewModel.HasLastResultMessage);
        Assert.Contains("is not registered", viewModel.LastResultMessage, StringComparison.Ordinal);
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

    [Fact]
    public void Non_contiguous_categories_group_under_first_seen_header()
    {
        var viewModel = new CommandPaletteViewModel(
            [
                new WorkbenchActionDescriptor(
                    "workbench.panel.scene-view",
                    "Scene View",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Scene View",
                    TargetId: "scene-view",
                    Category: "Window"),
                new WorkbenchActionDescriptor(
                    "workbench.commandPalette.open",
                    "Command Palette",
                    WorkbenchActionKind.OpenCommandPalette,
                    "Tools/Command Palette",
                    Category: "Tools"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.console",
                    "Console",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Console",
                    TargetId: "console",
                    Category: "Window"),
            ],
            commandId => WorkbenchCommandExecutionResult.Success(commandId));
        viewModel.OpenCommand.Execute(null);

        Assert.Equal(
            ["Window", "Scene View", "Console", "Tools", "Command Palette"],
            viewModel.FilteredItems.Select(item => item.Title));
    }

    [Fact]
    public void Filtered_non_contiguous_categories_keep_registered_category_order()
    {
        var viewModel = new CommandPaletteViewModel(
            [
                new WorkbenchActionDescriptor(
                    "workbench.panel.scene-view",
                    "Scene View",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Scene View",
                    TargetId: "scene-view",
                    Category: "Window"),
                new WorkbenchActionDescriptor(
                    "workbench.commandPalette.open",
                    "Command Palette",
                    WorkbenchActionKind.OpenCommandPalette,
                    "Tools/Command Palette",
                    Category: "Tools",
                    SearchText: "palette-filter"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.console",
                    "Console",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Console",
                    TargetId: "console",
                    Category: "Window",
                    SearchText: "palette-filter"),
            ],
            commandId => WorkbenchCommandExecutionResult.Success(commandId));
        viewModel.OpenCommand.Execute(null);

        viewModel.Query = "palette-filter";

        Assert.Equal(
            ["Window", "Console", "Tools", "Command Palette"],
            viewModel.FilteredItems.Select(item => item.Title));
    }

    private static IReadOnlyList<CommandPaletteItemViewModel> CommandRows(CommandPaletteViewModel viewModel)
    {
        return viewModel.FilteredItems.Where(item => item.IsCommand).ToArray();
    }

    private static void ExecuteMatchingCommand(CommandPaletteViewModel viewModel, string query)
    {
        viewModel.Query = query;
        viewModel.ExecuteSelectedCommand.Execute(null);
        viewModel.OpenCommand.Execute(null);
    }

    private static CommandPaletteViewModel CreatePaletteWithCommandCount(int commandCount)
    {
        return new CommandPaletteViewModel(
            Enumerable.Range(1, commandCount)
                .Select(index => new WorkbenchActionDescriptor(
                    $"test.command-{index}",
                    $"Command {index}",
                    WorkbenchActionKind.OpenCommandPalette,
                    $"Tools/Command {index}",
                    Category: "Tools"))
                .ToArray(),
            commandId => WorkbenchCommandExecutionResult.Success(commandId));
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
