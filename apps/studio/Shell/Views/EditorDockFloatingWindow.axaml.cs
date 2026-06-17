using System;
using Avalonia.Controls;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Views;

public partial class EditorDockFloatingWindow : Window
{
    public EditorDockFloatingWindow()
    {
        InitializeComponent();
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        EditorDockFloatingWindowRegistry.Register(this);
    }

    protected override void OnClosed(EventArgs e)
    {
        EditorDockFloatingWindowRegistry.Unregister(this);
        base.OnClosed(e);
    }
}
