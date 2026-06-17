using System;
using Avalonia.Controls;
using Avalonia;
using Editor.Shell.ViewModels;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Views;

public partial class MainWindow : Window
{
    private bool restoredFloatingWindows_;

    public MainWindow()
    {
        InitializeComponent();
        DataContextChanged += OnMainWindowDataContextChanged;
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        RestoreFloatingWindows();
    }

    private void OnMainWindowDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            viewModel.SetFloatingWindowCallbacks(
                EditorDockFloatingWindowRegistry.CaptureSnapshots,
                EditorDockFloatingWindowRegistry.CloseAll,
                EditorDockFloatingWindowRegistry.TryActivatePanel);
        }
    }

    private void RestoreFloatingWindows()
    {
        if (restoredFloatingWindows_ || DataContext is not MainWindowViewModel viewModel)
        {
            return;
        }

        restoredFloatingWindows_ = true;
        foreach (var request in viewModel.ConsumeRestoredFloatingWindowRequests())
        {
            ShowFloatingWindow(request);
        }
    }

    private void ShowFloatingWindow(EditorDockFloatingWindowRequest request)
    {
        var bounds = EditorDockFloatingWindowPlacement.NormalizeBounds(request.Bounds);
        var window = new EditorDockFloatingWindow
        {
            DataContext = request.Window,
            Width = bounds.Width,
            Height = bounds.Height,
            Position = EditorDockFloatingWindowPlacement.ClampPosition(
                this,
                EditorDockFloatingWindowPlacement.ToPixelPoint(new Point(bounds.X, bounds.Y)),
                bounds.Width,
                bounds.Height),
        };
        window.Show(this);
    }
}
