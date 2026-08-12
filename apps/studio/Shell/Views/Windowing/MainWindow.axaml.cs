using System.Linq;
using System;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Editor.Shell.Actions;
using Editor.Shell.Commands;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.Views.Windowing;

public partial class MainWindow : Window
{
    private StudioShellViewModel? subscribedShell_;

    public MainWindow()
    {
        InitializeComponent();
        AddHandler(
            KeyDownEvent,
            OnUnhandledKeyDown,
            RoutingStrategies.Bubble,
            handledEventsToo: false);
        DataContextChanged += OnWindowDataContextChanged;
    }

    private void OnWindowDataContextChanged(object? sender, System.EventArgs e)
    {
        if (subscribedShell_ is not null)
        {
            subscribedShell_.ActionStateChanged -= OnActionStateChanged;
            subscribedShell_ = null;
        }

        if (DataContext is StudioShellViewModel shell)
        {
            subscribedShell_ = shell;
            shell.ActionStateChanged += OnActionStateChanged;
        }
        RebuildMainMenu();
    }

    protected override void OnClosed(EventArgs e)
    {
        if (subscribedShell_ is not null)
        {
            subscribedShell_.ActionStateChanged -= OnActionStateChanged;
            subscribedShell_ = null;
        }
        DataContextChanged -= OnWindowDataContextChanged;
        base.OnClosed(e);
    }

    private void OnActionStateChanged(object? sender, EventArgs e) =>
        RebuildMainMenu();

    private void RebuildMainMenu()
    {
        StudioMainMenu.Items.Clear();
        if (DataContext is not StudioShellViewModel shell)
        {
            return;
        }

        foreach (var item in StudioActionMenuProjector.ProjectTopLevelMenus(
                     shell,
                     StudioShellPresentationIds.MainWindow,
                     StudioShellViewModel.ActivePanelId(shell.DockWorkspace)))
        {
            StudioMainMenu.Items.Add(item);
        }
        AppendLifetimeExitItem();
    }

    private void AppendLifetimeExitItem()
    {
        var fileMenu = StudioMainMenu.Items
            .OfType<MenuItem>()
            .FirstOrDefault(item => string.Equals(item.Header?.ToString(), "File",
                System.StringComparison.Ordinal));
        if (fileMenu is null)
        {
            return;
        }

        fileMenu.Items.Add(new Separator());
        var exit = new MenuItem
        {
            Header = "Exit",
        };
        exit.Tag = "studio-lifetime.exit";
        exit.Click += OnExitMenuItemClick;
        fileMenu.Items.Add(exit);
    }

    private void OnUnhandledKeyDown(object? sender, KeyEventArgs e)
    {
        if (DataContext is StudioShellViewModel viewModel &&
            StudioActionShortcutRouter.TryRoute(
                viewModel,
                StudioShellPresentationIds.MainWindow,
                StudioShellViewModel.ActivePanelId(viewModel.DockWorkspace),
                FocusManager?.GetFocusedElement(),
                e))
        {
            e.Handled = true;
        }
    }

    private void OnExitMenuItemClick(object? sender, RoutedEventArgs e)
    {
        e.Handled = true;
        Close();
    }
}
