using Editor.UI.Icons;
using Editor.Core.Models.Workbench;
using Editor.Shell.ViewModels.Menus;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Menus;

public sealed class WorkbenchMenuItemViewModelTests
{
    [Fact]
    public void Constructor_projects_action_metadata()
    {
        var action = new WorkbenchActionDescriptor(
            "workbench.commandPalette.open",
            "Command Palette",
            WorkbenchActionKind.OpenCommandPalette,
            "Tools/Command Palette",
            IconKey: EditorIconKey.UiSearch,
            Category: "Tools",
            DefaultShortcut: "Ctrl+Shift+P");

        var item = new WorkbenchMenuItemViewModel(
            action,
            commandId => WorkbenchCommandExecutionResult.Success(commandId));

        Assert.Equal("workbench.commandPalette.open", item.CommandId);
        Assert.Equal("Command Palette", item.Header);
        Assert.Equal("Tools/Command Palette", item.MenuPath);
        Assert.Equal(EditorIconKey.UiSearch, item.IconKey);
        Assert.Equal("Ctrl+Shift+P", item.ShortcutText);
        Assert.True(item.HasShortcut);
        Assert.True(item.IsEnabled);
        Assert.Null(item.DisabledReason);
        Assert.False(item.HasDisabledReason);
    }

    [Fact]
    public void OpenCommand_dispatches_enabled_action_by_command_id()
    {
        var action = CreateToolAction();
        string? executedCommandId = null;
        var item = new WorkbenchMenuItemViewModel(
            action,
            commandId =>
            {
                executedCommandId = commandId;
                return WorkbenchCommandExecutionResult.Success(commandId);
            });

        item.OpenCommand.Execute(null);

        Assert.Equal("workbench.commandPalette.open", executedCommandId);
    }

    [Fact]
    public void OpenCommand_does_not_dispatch_disabled_action()
    {
        var action = CreateToolAction() with
        {
            IsEnabled = false,
            DisabledReason = "Disabled by test",
        };
        var dispatched = false;
        var item = new WorkbenchMenuItemViewModel(
            action,
            commandId =>
            {
                dispatched = true;
                return WorkbenchCommandExecutionResult.Success(commandId);
            });

        item.OpenCommand.Execute(null);

        Assert.False(item.OpenCommand.CanExecute(null));
        Assert.False(dispatched);
        Assert.Equal("Disabled by test", item.DisabledReason);
        Assert.True(item.HasDisabledReason);
    }

    private static WorkbenchActionDescriptor CreateToolAction()
    {
        return new WorkbenchActionDescriptor(
            "workbench.commandPalette.open",
            "Command Palette",
            WorkbenchActionKind.OpenCommandPalette,
            "Tools/Command Palette",
            Category: "Tools",
            DefaultShortcut: "Ctrl+Shift+P");
    }
}
