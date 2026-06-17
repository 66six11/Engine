using System;
using Avalonia.Controls;
using Avalonia;
using Editor.Shell.ViewModels;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Views;

public partial class MainWindow : Window
{
    private bool restoredFloatingWindows_;
    private bool isDockHostFocused_ = true;

    public MainWindow()
    {
        InitializeComponent();
        Activated += OnWindowActivated;
        Deactivated += OnWindowDeactivated;
        DataContextChanged += OnMainWindowDataContextChanged;
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        SetDockHostFocusState(IsActive);
        RestoreFloatingWindows();
    }

    private void OnMainWindowDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            RebuildPanelsMenu(viewModel);
            viewModel.SetFloatingWindowCallbacks(
                EditorDockFloatingWindowRegistry.CaptureSnapshots,
                EditorDockFloatingWindowRegistry.CloseAll,
                EditorDockFloatingWindowRegistry.TryActivatePanel);
            isDockHostFocused_ = IsActive;
            viewModel.DockWorkspace.SetHostFocusState(isDockHostFocused_);
            return;
        }

        RebuildPanelsMenu(null);
    }

    private void RebuildPanelsMenu(MainWindowViewModel? viewModel)
    {
        PanelsMenu.Items.Clear();
        if (viewModel is null)
        {
            return;
        }

        foreach (var panelItem in viewModel.PanelMenuItems)
        {
            var menuItem = new MenuItem
            {
                Header = panelItem.Header,
                Command = panelItem.OpenCommand,
            };
            menuItem.Classes.Add("editor-menu-item");
            PanelsMenu.Items.Add(menuItem);
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
        if (DataContext is MainWindowViewModel viewModel)
        {
            viewModel.DockWorkspace.SetHostFocusState(isFocused);
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
                bounds.Height,
                RenderScaling),
        };
        window.Show(this);
    }
}
