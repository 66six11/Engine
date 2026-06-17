using System;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class PanelMenuItemViewModel : ViewModelBase
{
    private bool isOpen_;

    public PanelMenuItemViewModel(
        PanelDescriptor descriptor,
        Action<string> openPanel)
    {
        PanelId = descriptor.Id;
        Header = descriptor.Title;
        IconKey = descriptor.IconKey;
        OpenCommand = new RelayCommand(() => openPanel(PanelId));
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
