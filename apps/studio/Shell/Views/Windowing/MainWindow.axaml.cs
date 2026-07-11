using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Threading;
using Editor.Core.Interop.Viewports.Adapters;
using Asharia.Editor.Lifecycle;
using Editor.UI.Icons;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Menus;
using Editor.Shell.Views.Docking;

namespace Editor.Shell.Views.Windowing;

public partial class MainWindow : Window
{
    private const string MainWindowLifecycleSource = "main-window";
    private readonly List<MenuItem> generatedToolsMenuItems_ = [];
    private readonly List<MenuItem> generatedHelpMenuItems_ = [];
    private readonly DispatcherTimer panelFrameTimer_ = new()
    {
        Interval = TimeSpan.FromMilliseconds(16),
    };
    private bool restoredFloatingWindows_;
    private bool isDockHostFocused_ = true;
    private bool isClosing_;
    private bool nativeViewportPresentDrainStarted_;
    private bool nativeViewportPresentDrainCompleted_;

    public MainWindow()
    {
        InitializeComponent();
        Activated += OnWindowActivated;
        Deactivated += OnWindowDeactivated;
        Closing += OnWindowClosing;
        KeyDown += OnMainWindowKeyDown;
        DataContextChanged += OnMainWindowDataContextChanged;
        PanelsMenu.SubmenuOpened += OnPanelsMenuSubmenuOpened;
        EditorDockFloatingWindowRegistry.DockContentChanged += OnFloatingDockContentChanged;
        panelFrameTimer_.Tick += OnPanelFrameTimerTick;
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationOpened);
        SetDockHostFocusState(IsActive);
        RestoreFloatingWindows();
        panelFrameTimer_.Start();
    }

    protected override void OnClosed(EventArgs e)
    {
        KeyDown -= OnMainWindowKeyDown;
        StopPanelFrameTimer();
        panelFrameTimer_.Tick -= OnPanelFrameTimerTick;
        Closing -= OnWindowClosing;
        EditorDockFloatingWindowRegistry.DockContentChanged -= OnFloatingDockContentChanged;
        PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationClosed);
        base.OnClosed(e);
    }

    private void OnPanelFrameTimerTick(object? sender, EventArgs e)
    {
        if (isClosing_)
        {
            return;
        }

        if (DataContext is MainWindowViewModel viewModel)
        {
            viewModel.DockWorkspace.PanelFrameScheduler.Tick(DateTimeOffset.UtcNow);
        }
    }

    private async void OnWindowClosing(object? sender, WindowClosingEventArgs e)
    {
        if (!isClosing_)
        {
            isClosing_ = true;
            StopPanelFrameTimer();
            PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationClosing);
        }

        ViewportNativePresentDrain.RequestShutdown();
        if (nativeViewportPresentDrainCompleted_ || !ViewportNativePresentDrain.HasActivePresents)
        {
            return;
        }

        e.Cancel = true;
        if (nativeViewportPresentDrainStarted_)
        {
            return;
        }

        nativeViewportPresentDrainStarted_ = true;
        await ViewportNativePresentDrain.WaitForIdleAsync(TimeSpan.FromSeconds(5));
        nativeViewportPresentDrainCompleted_ = true;
        Close();
    }

    private void StopPanelFrameTimer()
    {
        panelFrameTimer_.Stop();
    }

    private void OnMainWindowDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            RebuildToolsMenu(viewModel);
            RebuildHelpMenu(viewModel);
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

        RebuildToolsMenu(null);
        RebuildHelpMenu(null);
        RebuildPanelsMenu(null);
    }

    private void RebuildToolsMenu(MainWindowViewModel? viewModel)
    {
        foreach (var menuItem in generatedToolsMenuItems_)
        {
            ToolsMenu.Items.Remove(menuItem);
        }

        generatedToolsMenuItems_.Clear();
        if (viewModel is null)
        {
            return;
        }

        var insertIndex = 0;
        foreach (var commandItem in viewModel.ToolsMenuItems)
        {
            var menuItem = CreateCommandMenuItem(commandItem);
            generatedToolsMenuItems_.Add(menuItem);
            ToolsMenu.Items.Insert(insertIndex, menuItem);
            insertIndex++;
        }
    }

    private void RebuildHelpMenu(MainWindowViewModel? viewModel)
    {
        foreach (var menuItem in generatedHelpMenuItems_)
        {
            HelpMenu.Items.Remove(menuItem);
        }

        generatedHelpMenuItems_.Clear();
        if (viewModel is null)
        {
            return;
        }

        var insertIndex = HelpMenu.Items.Count;
        foreach (var commandItem in viewModel.HelpMenuItems)
        {
            var menuItem = CreateCommandMenuItem(commandItem);
            generatedHelpMenuItems_.Add(menuItem);
            HelpMenu.Items.Insert(insertIndex, menuItem);
            insertIndex++;
        }
    }

    private static MenuItem CreateCommandMenuItem(WorkbenchMenuItemViewModel commandItem)
    {
        var menuItem = new MenuItem
        {
            DataContext = commandItem,
            Header = CreateCommandMenuHeader(commandItem),
            Command = commandItem.OpenCommand,
            IsEnabled = commandItem.IsEnabled,
        };
        menuItem.Classes.Add("editor-menu-item");

        if (commandItem.HasDisabledReason)
        {
            ToolTip.SetTip(menuItem, commandItem.DisabledReason);
        }

        return menuItem;
    }

    private static Grid CreateCommandMenuHeader(WorkbenchMenuItemViewModel commandItem)
    {
        var header = new Grid
        {
            DataContext = commandItem,
            ColumnDefinitions = new ColumnDefinitions("*,Auto"),
        };

        var title = new TextBlock
        {
            Text = commandItem.Header,
            VerticalAlignment = VerticalAlignment.Center,
        };
        header.Children.Add(title);

        if (commandItem.HasShortcut)
        {
            var shortcut = new TextBlock
            {
                Text = commandItem.ShortcutText,
                Margin = new Thickness(32, 0, 0, 0),
                Opacity = 0.7,
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Center,
            };
            Grid.SetColumn(shortcut, 1);
            header.Children.Add(shortcut);
        }

        return header;
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

    private void PublishLifecycleEvent(EditorLifecycleEventKind kind, string? message = null)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            PublishLifecycleEvent(viewModel, kind, MainWindowLifecycleSource, message);
        }
    }

    internal static EditorLifecycleEventSnapshot? PublishLifecycleEvent(
        MainWindowViewModel? viewModel,
        EditorLifecycleEventKind kind,
        string source,
        string? message = null)
    {
        return viewModel?.LifecycleEvents.Publish(kind, source, message);
    }

    private void OnWindowActivated(object? sender, EventArgs e)
    {
        SetDockHostFocusState(true);
        PublishLifecycleEvent(EditorLifecycleEventKind.HostActivated);
    }

    private void OnWindowDeactivated(object? sender, EventArgs e)
    {
        SetDockHostFocusState(false);
        PublishLifecycleEvent(EditorLifecycleEventKind.HostDeactivated);
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

        PublishLifecycleEvent(EditorLifecycleEventKind.WorkspaceRestored);
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
