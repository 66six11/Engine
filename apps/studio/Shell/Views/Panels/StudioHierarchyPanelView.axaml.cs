using System;
using System.Linq;
using Asharia.Studio.Application.Actions;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Editor.Shell.Actions;
using Editor.Shell.ViewModels.Panels;
using Editor.Shell.Views.Docking;

namespace Editor.Shell.Views.Panels;

public partial class StudioHierarchyPanelView : UserControl
{
    public StudioHierarchyPanelView()
    {
        InitializeComponent();
    }

    private void OnHierarchyExpanderClick(object? sender, RoutedEventArgs e)
    {
        if (sender is Button { DataContext: StudioHierarchyRowViewModel row }
            && DataContext is StudioHierarchyPanelViewModel viewModel)
        {
            viewModel.ToggleExpanded(row);
            e.Handled = true;
        }
    }

    private void OnHierarchyRowDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: StudioHierarchyRowViewModel row }
            && DataContext is StudioHierarchyPanelViewModel viewModel
            && row.HasChildren)
        {
            viewModel.ToggleExpanded(row);
            e.Handled = true;
        }
    }

    private void OnHierarchyContextRequested(
        object? sender,
        ContextRequestedEventArgs e)
    {
        if (sender is not Control { DataContext: StudioHierarchyRowViewModel row } target ||
            DataContext is not StudioHierarchyPanelViewModel viewModel)
        {
            return;
        }

        var topLevel = TopLevel.GetTopLevel(this);
        var topLevelId = topLevel is EditorDockFloatingWindow floatingWindow
            ? floatingWindow.ActionTopLevelId
            : StudioShellPresentationIds.MainWindow;
        var workspace = topLevel is EditorDockFloatingWindow
            ? (topLevel.DataContext as ViewModels.Docking.EditorDockFloatingWindowViewModel)
                ?.DockWorkspace
            : viewModel.Shell.DockWorkspace;
        var menu = CreateActionContextMenu(
            row,
            topLevelId,
            workspace is null
                ? null
                : ViewModels.Windowing.StudioShellViewModel.ActivePanelId(workspace));
        if (menu is null)
        {
            return;
        }

        menu.Open(target);
        e.Handled = true;
    }

    internal ContextMenu? CreateActionContextMenu(
        StudioHierarchyRowViewModel row,
        StudioPresentationId topLevelId,
        StudioPresentationId? focusedPanelId = null)
    {
        ArgumentNullException.ThrowIfNull(row);
        if (DataContext is not StudioHierarchyPanelViewModel viewModel ||
            !TryCreateFrozenTarget(viewModel, row, out var target))
        {
            return null;
        }

        var context = viewModel.Shell.CaptureActionContext(
            StudioActionInvocationSource.ContextMenu,
            topLevelId,
            target,
            focusedPanelId);
        var menu = new ContextMenu
        {
            DataContext = new StudioHierarchyActionMenuContext(
                viewModel.Shell,
                context),
        };
        foreach (var item in Commands.StudioActionMenuProjector.ProjectContextMenu(
                     viewModel.Shell,
                     context,
                     "Hierarchy"))
        {
            menu.Items.Add(item);
        }
        return menu;
    }

    private static bool TryCreateFrozenTarget(
        StudioHierarchyPanelViewModel viewModel,
        StudioHierarchyRowViewModel row,
        out StudioActionTarget target)
    {
        var snapshot = viewModel.Shell.AppliedProjectSnapshot;
        if (snapshot.Project is not { } project || snapshot.Document is not { } document ||
            (row.IsSceneRoot
                ? row.StableId != document.SceneId
                : !document.Entities.Any(entity => entity.ObjectId == row.StableId)))
        {
            target = StudioActionTarget.None;
            return false;
        }

        target = row.IsSceneRoot
            ? StudioActionTarget.Scene(project.SessionId, document.SceneId)
            : StudioActionTarget.SceneObject(
                project.SessionId,
                document.SceneId,
                row.StableId);
        return true;
    }
}

internal sealed record StudioHierarchyActionMenuContext(
    ViewModels.Windowing.StudioShellViewModel Shell,
    StudioActionContextSnapshot Snapshot);
