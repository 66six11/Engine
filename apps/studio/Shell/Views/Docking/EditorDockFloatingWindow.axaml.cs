using System;
using Asharia.Studio.Application.Actions;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Editor.Shell.Commands;
using Editor.Shell.Docking.Lifecycle;
using Editor.Shell.Lifecycle;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Views.Docking;

public partial class EditorDockFloatingWindow : Window
{
    private const string FloatingWindowLifecycleSource = "floating-window";
    private readonly StudioPresentationId actionTopLevelId_ = new(
        $"floating-window:{Guid.NewGuid():N}");
    private bool isDockHostFocused_ = true;

    public EditorDockFloatingWindow()
    {
        InitializeComponent();
        Activated += OnWindowActivated;
        Deactivated += OnWindowDeactivated;
        DataContextChanged += OnFloatingWindowDataContextChanged;
        AddHandler(
            KeyDownEvent,
            OnUnhandledKeyDown,
            RoutingStrategies.Bubble,
            handledEventsToo: false);
    }

    internal StudioPresentationId ActionTopLevelId => actionTopLevelId_;

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
        var exceptions = new CallbackExceptionBatch();
        exceptions.Capture(
            () => EditorDockFloatingWindowRegistry.Unregister(this));
        exceptions.Capture(
            () => PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowClosed));
        exceptions.Capture(
            () => DisposeFloatingWindowViewModel(viewModel));
        try
        {
            base.OnClosed(e);
        }
        catch (Exception exception)
        {
            exceptions.Add(exception);
        }

        exceptions.ThrowIfAny();
    }

    private void OnFloatingWindowDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is EditorDockFloatingWindowViewModel viewModel)
        {
            isDockHostFocused_ = IsActive;
            viewModel.DockWorkspace.SetHostFocusState(isDockHostFocused_);
        }
    }

    private void OnUnhandledKeyDown(object? sender, KeyEventArgs e)
    {
        if (TryResolveShell(out var shell) &&
            StudioActionShortcutRouter.TryRoute(
                shell,
                ActionTopLevelId,
                DataContext is EditorDockFloatingWindowViewModel viewModel
                    ? StudioShellViewModel.ActivePanelId(viewModel.DockWorkspace)
                    : null,
                FocusManager?.GetFocusedElement(),
                e))
        {
            e.Handled = true;
        }
    }

    private bool TryResolveShell(out StudioShellViewModel shell)
    {
        WindowBase? candidate = this;
        while (candidate is not null)
        {
            if (candidate is MainWindow { DataContext: StudioShellViewModel resolved })
            {
                shell = resolved;
                return true;
            }
            candidate = candidate.Owner;
        }

        shell = null!;
        return false;
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
