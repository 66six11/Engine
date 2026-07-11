using System;
using Avalonia.Controls;
using Asharia.Editor.Lifecycle;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Views.Docking;

public partial class EditorDockFloatingWindow : Window
{
    private const string FloatingWindowLifecycleSource = "floating-window";
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
        PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowOpened);
    }

    protected override void OnClosed(EventArgs e)
    {
        var viewModel = DataContext as EditorDockFloatingWindowViewModel;
        EditorDockFloatingWindowRegistry.Unregister(this);
        PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowClosed);
        DisposeFloatingWindowViewModel(viewModel);
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
        PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowActivated);
    }

    private void OnWindowDeactivated(object? sender, EventArgs e)
    {
        SetDockHostFocusState(false);
        PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowDeactivated);
    }

    private void PublishLifecycleEvent(EditorLifecycleEventKind kind, string? message = null)
    {
        if (DataContext is EditorDockFloatingWindowViewModel viewModel)
        {
            PublishLifecycleEvent(viewModel, kind, FloatingWindowLifecycleSource, message);
        }
    }

    internal static EditorLifecycleEventSnapshot? PublishLifecycleEvent(
        EditorDockFloatingWindowViewModel? viewModel,
        EditorLifecycleEventKind kind,
        string source,
        string? message = null)
    {
        return viewModel?.LifecycleEvents.Publish(kind, source, message);
    }

    internal static void DisposeFloatingWindowViewModel(EditorDockFloatingWindowViewModel? viewModel)
    {
        viewModel?.Dispose();
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
