using Asharia.Editor.Dialogs;
using CommunityToolkit.Mvvm.Input;

namespace Editor.Shell.ViewModels.Dialogs;

public sealed class EditorDialogButtonViewModel
{
    internal EditorDialogButtonViewModel(
        EditorDialogActionDescriptor descriptor,
        IRelayCommand command)
    {
        Descriptor = descriptor;
        Command = command;
    }

    internal EditorDialogActionDescriptor Descriptor { get; }

    public string Id => Descriptor.Id.Value;

    public string Text => Descriptor.Text;

    public EditorDialogActionRole Role => Descriptor.Role;

    public bool IsDefault => Descriptor.IsDefault;

    public bool IsDestructive => Descriptor.IsDestructive;

    public IRelayCommand Command { get; }
}
