using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using Editor.Shell.Icons;
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
        KeyDown += OnMainWindowKeyDown;
        DataContextChanged += OnMainWindowDataContextChanged;
        PanelsMenu.SubmenuOpened += OnPanelsMenuSubmenuOpened;
        EditorDockFloatingWindowRegistry.DockContentChanged += OnFloatingDockContentChanged;
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        SetDockHostFocusState(IsActive);
        RestoreFloatingWindows();
    }

    protected override void OnClosed(EventArgs e)
    {
        KeyDown -= OnMainWindowKeyDown;
        EditorDockFloatingWindowRegistry.DockContentChanged -= OnFloatingDockContentChanged;
        base.OnClosed(e);
    }

    private void OnMainWindowDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            RebuildPanelsMenu(viewModel);
            viewModel.SetFloatingWindowCallbacks(
                EditorDockFloatingWindowRegistry.CaptureSnapshots,
                EditorDockFloatingWindowRegistry.CloseAll,
                EditorDockFloatingWindowRegistry.TryActivatePanel,
                EditorDockFloatingWindowRegistry.ContainsPanel,
                EditorDockFloatingWindowRegistry.TryClosePanel);
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
                DataContext = panelItem,
                Header = CreatePanelMenuHeader(panelItem),
                Command = panelItem.OpenCommand,
            };
            menuItem.Classes.Add("editor-menu-item");
            PanelsMenu.Items.Add(menuItem);
        }
    }

    private static Grid CreatePanelMenuHeader(PanelMenuItemViewModel panelItem)
    {
        var header = new Grid
        {
            DataContext = panelItem,
            ColumnDefinitions = new ColumnDefinitions("*,Auto"),
        };

        var title = new TextBlock
        {
            Text = panelItem.Header,
            VerticalAlignment = VerticalAlignment.Center,
        };
        header.Children.Add(title);

        var openIndicator = new EditorIconView
        {
            IconKey = EditorIconKey.UiCheck,
            IconSize = 12,
            StrokeWidth = 2,
        };
        openIndicator.Classes.Add("editor-menu-open-indicator");

        var openIndicatorSlot = new Border
        {
            Child = openIndicator,
            Margin = new Thickness(24, 0, 0, 0),
            HorizontalAlignment = HorizontalAlignment.Right,
            VerticalAlignment = VerticalAlignment.Center,
        };
        openIndicatorSlot.Bind(
            Visual.IsVisibleProperty,
            new Binding(nameof(PanelMenuItemViewModel.IsOpen)));
        Grid.SetColumn(openIndicatorSlot, 1);
        header.Children.Add(openIndicatorSlot);

        return header;
    }

    private void OnPanelsMenuSubmenuOpened(object? sender, EventArgs e)
    {
        RefreshPanelMenuOpenStates();
    }

    private void OnFloatingDockContentChanged(object? sender, EventArgs e)
    {
        RefreshPanelMenuOpenStates();
    }

    private void RefreshPanelMenuOpenStates()
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            viewModel.RefreshPanelMenuOpenStates();
        }
    }

    private void OnMainWindowKeyDown(object? sender, KeyEventArgs e)
    {
        if (DataContext is not MainWindowViewModel viewModel)
        {
            return;
        }

        var result = viewModel.ExecuteShortcut(
            e.Key,
            e.KeyModifiers,
            IsTextInputShortcutSource(e.Source));
        if (result is not null)
        {
            e.Handled = true;
        }
    }

    internal static bool IsTextInputShortcutSource(object? source)
    {
        return source is TextBox;
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
