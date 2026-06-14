using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using Editor.Core.Models;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockWorkspaceView : UserControl
{
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

        UpdateDropPreview(workspace, point);
        workspace.CompleteDrag(workspace.DragState.DropArea ?? DockArea.Center);
    }

    public void CancelTabDrag()
    {
        if (DataContext is EditorDockWorkspaceViewModel workspace)
        {
            workspace.CancelDrag();
        }
    }

    private void UpdateDropPreview(EditorDockWorkspaceViewModel workspace, Point point)
    {
        var (area, target) = ResolveDropTarget(point);
        workspace.DragState.UpdateDropPreview(area, target.X, target.Y, target.Width, target.Height);
    }

    private (DockArea Area, Rect Bounds) ResolveDropTarget(Point point)
    {
        var left = GetHostBounds(LeftPaneHost);
        var center = GetHostBounds(CenterPaneHost);
        var bottom = GetHostBounds(BottomPaneHost);
        var right = GetHostBounds(RightPaneHost);

        if (left.Contains(point))
        {
            return (DockArea.Left, left);
        }

        if (right.Contains(point))
        {
            return (DockArea.Right, right);
        }

        if (bottom.Contains(point))
        {
            return (DockArea.Bottom, bottom);
        }

        if (center.Contains(point))
        {
            return (DockArea.Center, center);
        }

        if (point.X <= Bounds.Width * 0.25)
        {
            return (DockArea.Left, left);
        }

        if (point.X >= Bounds.Width * 0.75)
        {
            return (DockArea.Right, right);
        }

        if (point.Y >= Bounds.Height * 0.68)
        {
            return (DockArea.Bottom, bottom);
        }

        return (DockArea.Center, center);
    }

    private Rect GetHostBounds(Control host)
    {
        var origin = host.TranslatePoint(new Point(0, 0), DockRoot) ?? default;
        return new Rect(origin, host.Bounds.Size);
    }
}
