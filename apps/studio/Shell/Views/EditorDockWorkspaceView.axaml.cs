using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Core.Models;
using Editor.Shell.Docking;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockWorkspaceView : UserControl
{
    private static readonly TimeSpan TabReorderAnimationDuration = TimeSpan.FromMilliseconds(120);
    private static readonly List<WeakReference<EditorDockWorkspaceView>> WorkspaceReferences = [];
    private readonly Dictionary<Control, DispatcherTimer> tabMoveAnimations_ = [];
    private readonly EditorDockHitTestService hitTestService_ = new();
    private EditorDockWorkspaceView? previewWorkspace_;

    public EditorDockWorkspaceView()
    {
        InitializeComponent();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        RegisterWorkspace(this);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        StopTabMoveAnimations();
        UnregisterWorkspace(this);
        base.OnDetachedFromVisualTree(e);
    }

    public void BeginTabDrag(EditorDockTabViewModel tab, Point point)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

        workspace.BeginDrag(tab, point.X, point.Y);
        previewWorkspace_ = this;
        UpdateRoutedDropPreview(workspace, point);
    }

    public void UpdateTabDrag(Point point)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

        workspace.UpdateDrag(point.X, point.Y);
        UpdateRoutedDropPreview(workspace, point);
    }

    public void CompleteTabDrag(Point point)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

        var routedTarget = UpdateRoutedDropPreview(workspace, point);
        var floatingWindowRequest = workspace.CompleteDragInto(routedTarget.Workspace, routedTarget.Target);
        if (floatingWindowRequest is not null)
        {
            ShowFloatingWindow(floatingWindowRequest);
        }

        ClearExternalPreview(routedTarget.View);
        previewWorkspace_ = null;
        CloseEmptyFloatingHost(workspace);
    }

    public void CancelTabDrag()
    {
        if (DataContext is EditorDockWorkspaceViewModel workspace)
        {
            workspace.CancelDrag();
        }

        ClearExternalPreview(previewWorkspace_);
        previewWorkspace_ = null;
    }

    private RoutedDockDropTarget UpdateRoutedDropPreview(
        EditorDockWorkspaceViewModel sourceWorkspace,
        Point sourcePoint)
    {
        var screenPoint = WorkspacePointToScreen(sourcePoint);
        var targetView = FindWorkspaceAtScreenPoint(screenPoint) ?? this;
        if (!targetView.TryGetLocalPoint(screenPoint, out var targetPoint)
            || targetView.DataContext is not EditorDockWorkspaceViewModel targetWorkspace)
        {
            targetView = this;
            targetPoint = sourcePoint;
            targetWorkspace = sourceWorkspace;
        }

        if (!ReferenceEquals(previewWorkspace_, targetView))
        {
            ClearExternalPreview(previewWorkspace_);
            previewWorkspace_ = targetView;
        }

        if (!ReferenceEquals(targetWorkspace, sourceWorkspace))
        {
            sourceWorkspace.ClearDropPreview();
            if (sourceWorkspace.DragState.DraggedTab is { } tab)
            {
                targetWorkspace.BeginExternalDragPreview(tab, targetPoint.X, targetPoint.Y);
            }
        }

        var target = targetView.UpdateDropPreview(targetWorkspace, targetPoint);
        return new RoutedDockDropTarget(targetView, targetWorkspace, target);
    }

    private EditorDockDropTarget UpdateDropPreview(EditorDockWorkspaceViewModel workspace, Point point)
    {
        var tabLayoutSnapshot = CaptureTabLayoutSnapshot();
        var target = ResolveDropTarget(point);
        workspace.DragState.UpdateDropPreview(target);
        if (workspace.PreviewTabInsert(target))
        {
            AnimateTabReorder(tabLayoutSnapshot);
        }

        return target;
    }

    private EditorDockDropTarget ResolveDropTarget(Point point)
    {
        return hitTestService_.HitTest(
            point,
            new Rect(new Point(0, 0), DockRoot.Bounds.Size),
            GetWindowBounds(),
            GetSplitterBounds());
    }

    private IReadOnlyList<EditorDockWindowBounds> GetWindowBounds()
    {
        var windows = new List<EditorDockWindowBounds>();

        foreach (var host in GetWindowViewsForHitTest())
        {
            if (host.DataContext is not EditorDockWindowViewModel window)
            {
                continue;
            }

            var bounds = GetHostBounds(host);
            var tabWellBounds = GetTabWellBounds(host);
            if (bounds.Width <= 0 || bounds.Height <= 0)
            {
                continue;
            }

            windows.Add(new EditorDockWindowBounds(
                window.Id,
                window.Area,
                bounds,
                tabWellBounds,
                GetTabBounds(host, window),
                AllowsWindowInsertion: true,
                IsDragSource: window.IsDragSourceWindow));
        }

        return windows;
    }

    private IEnumerable<EditorDockWindowView> GetWindowViewsForHitTest()
    {
        foreach (var dockedWindow in DockLayout.GetVisualDescendants().OfType<EditorDockWindowView>())
        {
            yield return dockedWindow;
        }
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

    private Dictionary<EditorDockTabViewModel, Rect> CaptureTabLayoutSnapshot()
    {
        var snapshot = new Dictionary<EditorDockTabViewModel, Rect>();
        foreach (var tabHost in GetTabHostsForAnimation())
        {
            if (TryGetRealTab(tabHost, out var tab))
            {
                snapshot[tab] = GetHostBounds(tabHost);
            }
        }

        return snapshot;
    }

    private void AnimateTabReorder(IReadOnlyDictionary<EditorDockTabViewModel, Rect> previousBounds)
    {
        Dispatcher.UIThread.Post(
            () =>
            {
                foreach (var tabHost in GetTabHostsForAnimation())
                {
                    if (!TryGetRealTab(tabHost, out var tab)
                        || !previousBounds.TryGetValue(tab, out var previousBoundsForTab))
                    {
                        continue;
                    }

                    var currentBounds = GetHostBounds(tabHost);
                    var deltaX = previousBoundsForTab.X - currentBounds.X;
                    var deltaY = previousBoundsForTab.Y - currentBounds.Y;
                    if (Math.Abs(deltaX) < 0.5 && Math.Abs(deltaY) < 0.5)
                    {
                        continue;
                    }

                    AnimateTabMove(tabHost, deltaX, deltaY);
                }
            },
            DispatcherPriority.Render);
    }

    private IEnumerable<Control> GetTabHostsForAnimation()
    {
        return DockLayout.GetVisualDescendants()
            .OfType<Control>()
            .Where(control =>
                control.IsVisible
                && control.Classes.Contains("owned-dock-tab")
                && TryGetRealTab(control, out _));
    }

    private void AnimateTabMove(Control tabHost, double deltaX, double deltaY)
    {
        StopTabMoveAnimation(tabHost);

        var transform = new TranslateTransform(deltaX, deltaY);
        tabHost.RenderTransform = transform;

        var startedAt = DateTime.UtcNow;
        var timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(16),
        };
        timer.Tick += (_, _) =>
        {
            var progress = Math.Clamp(
                (DateTime.UtcNow - startedAt).TotalMilliseconds / TabReorderAnimationDuration.TotalMilliseconds,
                0d,
                1d);
            var eased = 1d - Math.Pow(1d - progress, 3d);
            transform.X = deltaX * (1d - eased);
            transform.Y = deltaY * (1d - eased);

            if (progress < 1d)
            {
                return;
            }

            StopTabMoveAnimation(tabHost);
        };

        tabMoveAnimations_[tabHost] = timer;
        timer.Start();
    }

    private void StopTabMoveAnimations()
    {
        foreach (var tabHost in tabMoveAnimations_.Keys.ToArray())
        {
            StopTabMoveAnimation(tabHost);
        }
    }

    private void StopTabMoveAnimation(Control tabHost)
    {
        if (tabMoveAnimations_.Remove(tabHost, out var timer))
        {
            timer.Stop();
        }

        tabHost.RenderTransform = null;
    }

    private Rect GetTabWellBounds(EditorDockWindowView host)
    {
        var tabWell = FindTabWellHost(host, "owned-dock-tab-well");
        return tabWell is null ? GetHostBounds(host) : GetHostBounds(tabWell);
    }

    private IReadOnlyList<EditorDockTabBounds> GetTabBounds(
        EditorDockWindowView host,
        EditorDockWindowViewModel window)
    {
        var tabBounds = new List<EditorDockTabBounds>();
        foreach (var tabHost in host.GetVisualDescendants().OfType<Control>())
        {
            if (!tabHost.IsVisible
                || !tabHost.Classes.Contains("owned-dock-tab")
                || !TryGetRealTab(tabHost, out var tab))
            {
                continue;
            }

            var tabIndex = window.Tabs.IndexOf(tab);
            var bounds = GetHostBounds(tabHost);
            if (tabIndex < 0 || bounds.Width <= 0 || bounds.Height <= 0)
            {
                continue;
            }

            tabBounds.Add(new EditorDockTabBounds(tab.Id, tabIndex, bounds, tab.IsDragSource));
        }

        tabBounds.Sort((left, right) => left.TabIndex.CompareTo(right.TabIndex));
        return tabBounds;
    }

    private static bool TryGetRealTab(Control tabHost, out EditorDockTabViewModel tab)
    {
        if (tabHost.DataContext is EditorDockTabViewModel directTab)
        {
            tab = directTab;
            return true;
        }

        if (tabHost.DataContext is EditorDockTabStripItemViewModel { IsPlaceholder: false } item)
        {
            tab = item.Tab;
            return true;
        }

        tab = null!;
        return false;
    }

    private static Control? FindTabWellHost(EditorDockWindowView host, string className)
    {
        return host.GetVisualDescendants()
            .OfType<Control>()
            .FirstOrDefault(control =>
                control.IsVisible
                && control.Bounds.Width > 0
                && control.Bounds.Height > 0
                && control.Classes.Contains(className));
    }

    private bool ContainsScreenPoint(PixelPoint screenPoint)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null || Bounds.Width <= 0 || Bounds.Height <= 0)
        {
            return false;
        }

        var scaling = topLevel.RenderScaling;
        var topLeft = WorkspacePointToScreen(new Point(0, 0));
        var right = topLeft.X + (Bounds.Width * scaling);
        var bottom = topLeft.Y + (Bounds.Height * scaling);
        return screenPoint.X >= topLeft.X
            && screenPoint.X <= right
            && screenPoint.Y >= topLeft.Y
            && screenPoint.Y <= bottom;
    }

    private bool TryGetLocalPoint(PixelPoint screenPoint, out Point point)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null)
        {
            point = default;
            return false;
        }

        var scaling = topLevel.RenderScaling;
        var topLeft = WorkspacePointToScreen(new Point(0, 0));
        point = new Point(
            (screenPoint.X - topLeft.X) / scaling,
            (screenPoint.Y - topLeft.Y) / scaling);
        return true;
    }

    private PixelPoint WorkspacePointToScreen(Point point)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null)
        {
            return new PixelPoint((int)point.X, (int)point.Y);
        }

        var topLevelPoint = this.TranslatePoint(point, topLevel) ?? point;
        return topLevel.PointToScreen(topLevelPoint);
    }

    private void ShowFloatingWindow(EditorDockFloatingWindowRequest request)
    {
        var window = new EditorDockFloatingWindow
        {
            DataContext = request.Window,
            Width = Math.Max(240, request.Bounds.Width),
            Height = Math.Max(180, request.Bounds.Height),
            Position = WorkspacePointToScreen(new Point(request.Bounds.X, request.Bounds.Y)),
        };

        if (TopLevel.GetTopLevel(this) is Window owner)
        {
            window.Show(owner);
            return;
        }

        window.Show();
    }

    private void CloseEmptyFloatingHost(EditorDockWorkspaceViewModel workspace)
    {
        if (!workspace.IsFloatingWindow || workspace.HasDockContent())
        {
            return;
        }

        if (TopLevel.GetTopLevel(this) is EditorDockFloatingWindow floatingWindow)
        {
            floatingWindow.Close();
        }
    }

    private void ClearExternalPreview(EditorDockWorkspaceView? workspace)
    {
        if (workspace is null || ReferenceEquals(workspace, this))
        {
            return;
        }

        if (workspace.DataContext is EditorDockWorkspaceViewModel viewModel)
        {
            viewModel.ClearExternalDragPreview();
        }
    }

    private static void RegisterWorkspace(EditorDockWorkspaceView workspace)
    {
        PruneWorkspaceReferences();
        foreach (var reference in WorkspaceReferences)
        {
            if (reference.TryGetTarget(out var existing) && ReferenceEquals(existing, workspace))
            {
                return;
            }
        }

        WorkspaceReferences.Add(new WeakReference<EditorDockWorkspaceView>(workspace));
    }

    private static void UnregisterWorkspace(EditorDockWorkspaceView workspace)
    {
        for (var index = WorkspaceReferences.Count - 1; index >= 0; index--)
        {
            if (!WorkspaceReferences[index].TryGetTarget(out var existing)
                || ReferenceEquals(existing, workspace))
            {
                WorkspaceReferences.RemoveAt(index);
            }
        }
    }

    private static EditorDockWorkspaceView? FindWorkspaceAtScreenPoint(PixelPoint screenPoint)
    {
        PruneWorkspaceReferences();
        for (var index = WorkspaceReferences.Count - 1; index >= 0; index--)
        {
            if (WorkspaceReferences[index].TryGetTarget(out var workspace)
                && workspace.ContainsScreenPoint(screenPoint))
            {
                return workspace;
            }
        }

        return null;
    }

    private static void PruneWorkspaceReferences()
    {
        for (var index = WorkspaceReferences.Count - 1; index >= 0; index--)
        {
            if (!WorkspaceReferences[index].TryGetTarget(out _))
            {
                WorkspaceReferences.RemoveAt(index);
            }
        }
    }

    private sealed record RoutedDockDropTarget(
        EditorDockWorkspaceView View,
        EditorDockWorkspaceViewModel Workspace,
        EditorDockDropTarget Target);
}
