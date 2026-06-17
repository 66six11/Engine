using System;
using Avalonia.Controls;
using Editor.Shell.ViewModels;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Views;

public partial class EditorDockFloatingWindow : Window
{
    private bool isDockHostFocused_ = true;

    public EditorDockFloatingWindow()
    {
        InitializeComponent();
        Activated += OnWindowActivated;
        Deactivated += OnWindowDeactivated;
        DataContextChanged += OnFloatingWindowDataContextChanged;
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        SetDockHostFocusState(IsActive);
        EditorDockFloatingWindowRegistry.Register(this);
    }

    protected override void OnClosed(EventArgs e)
    {
        EditorDockFloatingWindowRegistry.Unregister(this);
        base.OnClosed(e);
    }

    private void OnFloatingWindowDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is EditorDockFloatingWindowViewModel viewModel)
        {
            isDockHostFocused_ = IsActive;
            viewModel.DockWorkspace.SetHostFocusState(isDockHostFocused_);
        }
    }

    private void OnWindowActivated(object? sender, EventArgs e)
    {
        SetDockHostFocusState(true);
    }

    private void OnWindowDeactivated(object? sender, EventArgs e)
    {
        SetDockHostFocusState(false);
    }

    private void SetDockHostFocusState(bool isFocused)
    {
        if (isDockHostFocused_ == isFocused)
        {
            return;
        }

        isDockHostFocused_ = isFocused;
        if (DataContext is EditorDockFloatingWindowViewModel viewModel)
        {
            viewModel.DockWorkspace.SetHostFocusState(isFocused);
        }
    }
}
