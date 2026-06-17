using System;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class PanelMenuItemViewModel : ViewModelBase
{
    public PanelMenuItemViewModel(
        PanelDescriptor descriptor,
        Action<string> openPanel)
    {
        PanelId = descriptor.Id;
        Header = descriptor.Title;
        OpenCommand = new RelayCommand(() => openPanel(PanelId));
    }

    public string PanelId { get; }

    public string Header { get; }

    public IRelayCommand OpenCommand { get; }
}
