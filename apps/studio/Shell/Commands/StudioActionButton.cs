using System;
using System.Linq;
using Asharia.Studio.Application.Actions;
using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using Editor.Shell.Actions;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Panels;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Docking;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Commands;

internal static class StudioActionButton
{
    public static readonly AttachedProperty<StudioActionId> ActionIdProperty =
        AvaloniaProperty.RegisterAttached<Button, StudioActionId>(
            "ActionId",
            typeof(StudioActionButton));

    static StudioActionButton()
    {
        ActionIdProperty.Changed.AddClassHandler<Button>(OnActionIdChanged);
    }

    public static StudioActionId GetActionId(Button button) =>
        button.GetValue(ActionIdProperty);

    public static void SetActionId(Button button, StudioActionId value) =>
        button.SetValue(ActionIdProperty, value);

    private static void OnActionIdChanged(
        Button button,
        AvaloniaPropertyChangedEventArgs e)
    {
        button.AttachedToVisualTree -= OnButtonAttached;
        button.DetachedFromVisualTree -= OnButtonDetached;
        button.DataContextChanged -= OnButtonDataContextChanged;
        button.AttachedToVisualTree += OnButtonAttached;
        button.DetachedFromVisualTree += OnButtonDetached;
        button.DataContextChanged += OnButtonDataContextChanged;
        RefreshCommand(button);
    }

    private static void OnButtonDataContextChanged(object? sender, EventArgs e)
    {
        if (sender is Button button)
        {
            RefreshCommand(button);
        }
    }

    private static void OnButtonAttached(
        object? sender,
        VisualTreeAttachmentEventArgs e)
    {
        if (sender is Button button)
        {
            RefreshCommand(button);
        }
    }

    private static void OnButtonDetached(
        object? sender,
        VisualTreeAttachmentEventArgs e)
    {
        if (sender is Button button)
        {
            button.Command = null;
        }
    }

    private static void RefreshCommand(Button button)
    {
        if (button.DataContext is StudioDockPanelViewModel panel &&
            GetActionId(button).IsValid &&
            TryResolvePresentation(
                button,
                panel.Shell,
                out var topLevelId,
                out var focusedPanelId))
        {
            button.Command = panel.Shell.GetActionCommand(
                GetActionId(button),
                StudioActionInvocationSource.Toolbar,
                topLevelId,
                focusedPanelId);
            return;
        }

        button.Command = null;
    }

    internal static bool TryResolvePresentation(
        Control control,
        StudioShellViewModel shell,
        out StudioPresentationId topLevelId,
        out StudioPresentationId? focusedPanelId)
    {
        ArgumentNullException.ThrowIfNull(control);
        ArgumentNullException.ThrowIfNull(shell);
        var panelId = control.GetVisualAncestors()
            .OfType<EditorDockPanelContentHost>()
            .Select(host => host.Panel?.Id)
            .FirstOrDefault(id => !string.IsNullOrWhiteSpace(id));
        if (TopLevel.GetTopLevel(control) is EditorDockFloatingWindow floating &&
            floating.DataContext is EditorDockFloatingWindowViewModel floatingViewModel)
        {
            topLevelId = floating.ActionTopLevelId;
            focusedPanelId = panelId is null
                ? StudioShellViewModel.ActivePanelId(floatingViewModel.DockWorkspace)
                : new StudioPresentationId(panelId);
            return true;
        }

        topLevelId = StudioShellPresentationIds.MainWindow;
        focusedPanelId = panelId is null
            ? StudioShellViewModel.ActivePanelId(shell.DockWorkspace)
            : new StudioPresentationId(panelId);
        return TopLevel.GetTopLevel(control) is MainWindow;
    }
}
