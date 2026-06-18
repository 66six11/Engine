using System;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class PanelMenuItemViewModel : ViewModelBase
{
    private bool isOpen_;

    public PanelMenuItemViewModel(
        WorkbenchActionDescriptor action,
        Func<WorkbenchActionDescriptor, bool> executeAction)
    {
        ArgumentNullException.ThrowIfNull(action);
        ArgumentNullException.ThrowIfNull(executeAction);
        if (action.Kind != WorkbenchActionKind.OpenPanel
            || string.IsNullOrWhiteSpace(action.TargetId))
        {
            throw new ArgumentException("Panel menu items require an OpenPanel action with a target panel id.", nameof(action));
        }

        PanelId = action.TargetId;
        Header = action.Title;
        IconKey = action.IconKey;
        OpenCommand = new RelayCommand(() => executeAction(action));
    }

    public string PanelId { get; }

    public string Header { get; }

    public string? IconKey { get; }

    public bool IsOpen
    {
        get => isOpen_;
        private set => SetProperty(ref isOpen_, value);
    }

    public IRelayCommand OpenCommand { get; }

    internal void SetOpenState(bool isOpen)
    {
        IsOpen = isOpen;
    }
}
