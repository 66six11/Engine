using System.Windows.Input;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.Views.Windowing;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        AddHandler(
            KeyDownEvent,
            OnUnhandledKeyDown,
            RoutingStrategies.Bubble,
            handledEventsToo: false);
    }

    private void OnUnhandledKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Handled || DataContext is not StudioShellViewModel viewModel ||
            FocusManager?.GetFocusedElement() is TextBox)
        {
            return;
        }

        var primaryModifier = e.KeyModifiers & (KeyModifiers.Control | KeyModifiers.Meta);
        var allowedModifiers = KeyModifiers.Control | KeyModifiers.Meta | KeyModifiers.Shift;
        if ((primaryModifier != KeyModifiers.Control && primaryModifier != KeyModifiers.Meta) ||
            (e.KeyModifiers & ~allowedModifiers) != KeyModifiers.None)
        {
            return;
        }

        ICommand? command = (e.Key, e.KeyModifiers.HasFlag(KeyModifiers.Shift)) switch
        {
            (Key.Z, false) => viewModel.UndoSceneCommand,
            (Key.Z, true) => viewModel.RedoSceneCommand,
            (Key.Y, false) => viewModel.RedoSceneCommand,
            _ => null,
        };
        if (command?.CanExecute(parameter: null) != true)
        {
            return;
        }

        command.Execute(parameter: null);
        e.Handled = true;
    }
}
