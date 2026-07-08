using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models.Dialogs;

namespace Editor.Shell.ViewModels.Dialogs;

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
