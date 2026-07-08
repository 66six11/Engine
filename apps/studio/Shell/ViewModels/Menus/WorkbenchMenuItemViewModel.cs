using System;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models.Workbench;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Menus;

public sealed class WorkbenchMenuItemViewModel : ViewModelBase
{
    private readonly Func<string, WorkbenchCommandExecutionResult> executeCommand_;

    public WorkbenchMenuItemViewModel(
        WorkbenchActionDescriptor action,
        Func<string, WorkbenchCommandExecutionResult> executeCommand)
    {
        ArgumentNullException.ThrowIfNull(action);
        ArgumentNullException.ThrowIfNull(executeCommand);
        if (string.IsNullOrWhiteSpace(action.Id))
        {
            throw new ArgumentException("Workbench menu items require a command id.", nameof(action));
        }

        if (string.IsNullOrWhiteSpace(action.Title))
        {
            throw new ArgumentException("Workbench menu items require a title.", nameof(action));
        }

        if (string.IsNullOrWhiteSpace(action.MenuPath))
        {
            throw new ArgumentException("Workbench menu items require a menu path.", nameof(action));
        }

        CommandId = action.Id;
        Header = action.Title;
        MenuPath = action.MenuPath;
        IconKey = action.IconKey;
        ShortcutText = action.DefaultShortcut ?? string.Empty;
        IsEnabled = action.IsEnabled;
        DisabledReason = action.DisabledReason;
        executeCommand_ = executeCommand;
        OpenCommand = new RelayCommand(Execute, () => IsEnabled);
    }

    public string CommandId { get; }

    public string Header { get; }

    public string MenuPath { get; }

    public string? IconKey { get; }

    public string ShortcutText { get; }

    public bool HasShortcut => !string.IsNullOrWhiteSpace(ShortcutText);

    public bool IsEnabled { get; }

    public string? DisabledReason { get; }

    public bool HasDisabledReason => !string.IsNullOrWhiteSpace(DisabledReason);

    public IRelayCommand OpenCommand { get; }

    private void Execute()
    {
        if (!IsEnabled)
        {
            return;
        }

        _ = executeCommand_(CommandId);
    }
}
