using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using Editor.Core.Models;
using Editor.Shell.Docking;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockWorkspaceView : UserControl
{
    private readonly EditorDockHitTestService hitTestService_ = new();

    public EditorDockWorkspaceView()
    {
        InitializeComponent();
    }

    public void BeginTabDrag(EditorDockTabViewModel tab, Point point)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

        workspace.BeginDrag(tab, point.X, point.Y);
        UpdateDropPreview(workspace, point);
    }

    public void UpdateTabDrag(Point point)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

        workspace.UpdateDrag(point.X, point.Y);
        UpdateDropPreview(workspace, point);
    }

    public void CompleteTabDrag(Point point)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

        var target = UpdateDropPreview(workspace, point);
        var floatingWindow = workspace.CompleteDrag(target);
        if (floatingWindow is not null)
        {
            ShowFloatingWindow(floatingWindow);
        }
    }

    public void CancelTabDrag()
    {
        if (DataContext is EditorDockWorkspaceViewModel workspace)
        {
            workspace.CancelDrag();
        }
    }

    private EditorDockDropTarget UpdateDropPreview(EditorDockWorkspaceViewModel workspace, Point point)
    {
        var target = ResolveDropTarget(point);
        workspace.DragState.UpdateDropPreview(target);
        return target;
    }

    private EditorDockDropTarget ResolveDropTarget(Point point)
    {
        return hitTestService_.HitTest(
            point,
            new Rect(new Point(0, 0), DockRoot.Bounds.Size),
            GetPaneBounds(),
            GetSplitterBounds());
    }

    private IReadOnlyList<EditorDockPaneBounds> GetPaneBounds()
    {
        var panes = new List<EditorDockPaneBounds>();

        foreach (var host in DockLayout.GetVisualDescendants().OfType<EditorDockPaneView>())
        {
            if (host.DataContext is not EditorDockPaneViewModel pane)
            {
                continue;
            }

            var bounds = GetHostBounds(host);
            if (bounds.Width <= 0 || bounds.Height <= 0)
            {
                continue;
            }

            panes.Add(new EditorDockPaneBounds(pane.Id, pane.Area, bounds));
        }

        return panes;
    }

    private IReadOnlyList<EditorDockSplitterBounds> GetSplitterBounds()
    {
        var splitters = new List<EditorDockSplitterBounds>();

        foreach (var splitter in DockLayout.GetVisualDescendants().OfType<GridSplitter>())
        {
            if (!splitter.Classes.Contains("owned-dock-layout-splitter")
                || splitter.DataContext is not EditorDockSplitNodeViewModel split)
            {
                continue;
            }

            var bounds = GetHostBounds(splitter);
            if (bounds.Width <= 0 || bounds.Height <= 0)
            {
                continue;
            }

            splitters.Add(new EditorDockSplitterBounds(split.Id, split.Orientation, bounds));
        }

        return splitters;
    }

    private Rect GetHostBounds(Control host)
    {
        var origin = host.TranslatePoint(new Point(0, 0), DockRoot) ?? default;
        return new Rect(origin, host.Bounds.Size);
    }

    private void ShowFloatingWindow(EditorDockFloatingWindowRequest request)
    {
        var window = new EditorDockFloatingWindow
        {
            DataContext = request.Window,
            Width = request.Bounds.Width,
            Height = request.Bounds.Height,
        };

        var owner = TopLevel.GetTopLevel(this) as Window;
        if (owner is not null)
        {
            window.Position = owner.PointToScreen(new Point(request.Bounds.X, request.Bounds.Y));
            window.Show(owner);
            return;
        }

        window.Show();
    }
}
