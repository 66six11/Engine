using Avalonia.Controls;

namespace Editor.Shell.Views;

public partial class EditorDockFloatingWindow : Window
{
    public EditorDockFloatingWindow()
    {
        InitializeComponent();
    }

    protected override void OnOpened(System.EventArgs e)
    {
        base.OnOpened(e);
        EditorDockFloatingWindowRegistry.Register(this);
    }

    protected override void OnClosed(System.EventArgs e)
    {
        EditorDockFloatingWindowRegistry.Unregister(this);
        base.OnClosed(e);
    }
}
