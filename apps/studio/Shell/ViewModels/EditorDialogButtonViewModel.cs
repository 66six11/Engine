using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDialogButtonViewModel
{
    internal EditorDialogButtonViewModel(
        EditorDialogButtonDescriptor descriptor,
        IRelayCommand command)
    {
        Descriptor = descriptor;
        Command = command;
    }

    internal EditorDialogButtonDescriptor Descriptor { get; }

    public string Id => Descriptor.Id;

    public string Text => Descriptor.Text;

    public bool IsDefault => Descriptor.IsDefault;

    public IRelayCommand Command { get; }
}
