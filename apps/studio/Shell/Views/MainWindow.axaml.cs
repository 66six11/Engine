using System;
using Avalonia.Controls;
using Avalonia;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class MainWindow : Window
{
    private bool restoredFloatingWindows_;

    public MainWindow()
    {
        InitializeComponent();
        DataContextChanged += OnMainWindowDataContextChanged;
    }

    protected override void OnOpened(System.EventArgs e)
    {
        base.OnOpened(e);
        RestoreFloatingWindows();
    }

    private void OnMainWindowDataContextChanged(object? sender, System.EventArgs e)
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
        var window = new EditorDockFloatingWindow
        {
            DataContext = request.Window,
            Width = Math.Max(240, request.Bounds.Width),
            Height = Math.Max(180, request.Bounds.Height),
            Position = new PixelPoint(
                (int)Math.Round(request.Bounds.X),
                (int)Math.Round(request.Bounds.Y)),
        };
        window.Show(this);
    }
}
