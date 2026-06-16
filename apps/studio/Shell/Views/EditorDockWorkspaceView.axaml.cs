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
    private readonly Dictionary<EditorDockTabViewModel, TabMoveAnimationState> tabMoveAnimations_ = [];
    private readonly DispatcherTimer tabMoveAnimationTimer_ = new()
    {
        Interval = TimeSpan.FromMilliseconds(16),
    };
    private readonly EditorDockHitTestService hitTestService_ = new();
    private DockHitTestSnapshot? hitTestSnapshot_;
    private double draggedDockTabPointerOffsetX_;
    private Size draggedDockTabPreviewSize_;
    private EditorDockWorkspaceView? previewWorkspace_;

    public EditorDockWorkspaceView()
    {
        InitializeComponent();
        tabMoveAnimationTimer_.Tick += OnTabMoveAnimationTick;
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

    public void BeginTabDrag(
        EditorDockTabViewModel tab,
        Point point,
        Size previewSize,
        double pointerOffsetX)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

        draggedDockTabPreviewSize_ = previewSize;
        draggedDockTabPointerOffsetX_ = pointerOffsetX;
        InvalidateHitTestSnapshot();
        EnsureDraggedDockTabPreviewContent(tab);
        workspace.BeginDrag(tab);
        previewWorkspace_ = this;
        UpdateRoutedDropPreview(workspace, point);
    }

    public void ActivateTab(EditorDockTabViewModel tab)
    {
        if (DataContext is EditorDockWorkspaceViewModel workspace)
        {
            workspace.ActivateTab(tab);
        }
    }

    public void PreviewTabReorder(
        EditorDockWindowViewModel window,
        EditorDockTabViewModel tab,
        int targetIndex)
    {
        if (window.IsLocalTabReorderPreviewCurrent(tab, targetIndex, showsTab: false))
        {
            return;
        }

        var tabLayoutSnapshot = CaptureTabLayoutSnapshot();
        var changed = window.HideDragSourceTab(tab);
        changed = window.ShowTabInsertPlaceholder(tab, targetIndex, showsTab: false) || changed;
        if (changed)
        {
            AnimateTabReorder(tabLayoutSnapshot);
        }
    }

    public void CompleteTabReorder(
        EditorDockWindowViewModel window,
        EditorDockTabViewModel tab,
        int targetIndex)
    {
        var tabLayoutSnapshot = CaptureTabLayoutSnapshot();
        var changed = window.ClearHiddenDragSourceTab();
        changed = window.ClearTabInsertPlaceholder() || changed;
        if (DataContext is EditorDockWorkspaceViewModel workspace)
        {
            changed = workspace.ReorderTabInWindow(window, tab, targetIndex) || changed;
        }

        if (changed)
        {
            AnimateTabReorder(tabLayoutSnapshot);
        }
    }

    public void CancelTabReorder(EditorDockWindowViewModel window)
    {
        var tabLayoutSnapshot = CaptureTabLayoutSnapshot();
        var changed = window.ClearHiddenDragSourceTab();
        changed = window.ClearTabInsertPlaceholder() || changed;
        if (changed)
        {
            AnimateTabReorder(tabLayoutSnapshot);
        }
    }

    public void UpdateTabDrag(Point point)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

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

        HideDraggedDockTabPreview();
        ClearPreviewWorkspace(routedTarget.View);
        previewWorkspace_ = null;
        CloseEmptyFloatingHost(workspace);
    }

    public void CancelTabDrag()
    {
        if (DataContext is EditorDockWorkspaceViewModel workspace)
        {
            workspace.CancelDrag();
        }

        HideDraggedDockTabPreview();
        ClearPreviewWorkspace(previewWorkspace_);
        previewWorkspace_ = null;
    }

    public void CloseTab(EditorDockTabViewModel tab)
    {
        if (DataContext is not EditorDockWorkspaceViewModel workspace)
        {
            return;
        }

        workspace.CloseTab(tab);
        CloseEmptyFloatingHost(workspace);
    }

    private RoutedDockDropTarget UpdateRoutedDropPreview(
        EditorDockWorkspaceViewModel sourceWorkspace,
        Point sourcePoint)
    {
        var screenPoint = WorkspacePointToScreen(sourcePoint);
        var targetView = FindWorkspaceTopLevelAtScreenPoint(screenPoint)
            ?? FindWorkspaceAtScreenPoint(screenPoint)
            ?? this;
        if (!targetView.TryGetLocalPoint(screenPoint, out var targetPoint)
            || targetView.DataContext is not EditorDockWorkspaceViewModel targetWorkspace)
        {
            targetView = this;
            targetPoint = sourcePoint;
            targetWorkspace = sourceWorkspace;
        }

        if (!ReferenceEquals(previewWorkspace_, targetView))
        {
            ClearPreviewWorkspace(previewWorkspace_);
            previewWorkspace_ = targetView;
        }

        if (!ReferenceEquals(targetWorkspace, sourceWorkspace))
        {
            sourceWorkspace.ClearDropPreview();
            HideDraggedDockTabPreview();
            if (sourceWorkspace.DragState.DraggedTab is { } tab)
            {
                targetView.BeginExternalDragPreview(
                    tab,
                    draggedDockTabPreviewSize_,
                    draggedDockTabPointerOffsetX_);
            }
        }

        var target = targetView.UpdateDropPreview(
            targetWorkspace,
            targetPoint,
            targetView.ContainsTopLevelScreenPoint(screenPoint));
        return new RoutedDockDropTarget(targetView, targetWorkspace, target);
    }

    private EditorDockDropTarget UpdateDropPreview(
        EditorDockWorkspaceViewModel workspace,
        Point point,
        bool allowOutOfBoundsWorkspaceEdge)
    {
        var target = ResolveDropTarget(point, allowOutOfBoundsWorkspaceEdge);
        workspace.DragState.UpdateDropPreview(target);
        if (workspace.WouldPreviewTabInsertChange(target))
        {
            var tabLayoutSnapshot = CaptureTabLayoutSnapshot();
            if (workspace.PreviewTabInsert(target))
            {
                AnimateTabReorder(tabLayoutSnapshot);
            }
        }

        UpdateDraggedDockTabPreview(workspace, point, target);

        return target;
    }

    private EditorDockDropTarget ResolveDropTarget(Point point, bool allowOutOfBoundsWorkspaceEdge)
    {
        var snapshot = GetHitTestSnapshot();
        return hitTestService_.HitTest(
            point,
            snapshot.WorkspaceBounds,
            snapshot.Windows,
            snapshot.Splitters,
            allowOutOfBoundsWorkspaceEdge,
            GetTabInsertProbeX(point));
    }

    private DockHitTestSnapshot GetHitTestSnapshot()
    {
        var dockRootSize = DockRoot.Bounds.Size;
        if (hitTestSnapshot_ is { DockRootSize: var cachedSize } snapshot
            && cachedSize == dockRootSize)
        {
            return snapshot;
        }

        var windows = GetWindowBounds();
        snapshot = new DockHitTestSnapshot(
            dockRootSize,
            new Rect(new Point(0, 0), dockRootSize),
            windows,
            GetSplitterBounds());
        hitTestSnapshot_ = snapshot;
        return snapshot;
    }

    private void InvalidateHitTestSnapshot()
    {
        hitTestSnapshot_ = null;
    }

    private double GetTabInsertProbeX(Point point)
    {
        if (draggedDockTabPreviewSize_.Width <= 0)
        {
            return point.X;
        }

        return point.X - draggedDockTabPointerOffsetX_ + (draggedDockTabPreviewSize_.Width / 2);
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
                window.Tabs.Count,
                GetTabBounds(host, window),
                GetDragSourceTabIndex(window),
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

            TryGetSplitterAdjacentBounds(splitter, split.Orientation, out var firstBounds, out var secondBounds);

            splitters.Add(new EditorDockSplitterBounds(
                split.Id,
                split.Orientation,
                bounds,
                firstBounds,
                secondBounds));
        }

        return splitters;
    }

    private bool TryGetSplitterAdjacentBounds(
        GridSplitter splitter,
        Avalonia.Layout.Orientation orientation,
        out Rect firstBounds,
        out Rect secondBounds)
    {
        firstBounds = default;
        secondBounds = default;
        if (splitter.Parent is not Grid grid)
        {
            return false;
        }

        Control? firstHost = null;
        Control? secondHost = null;
        foreach (var child in grid.Children.OfType<Control>())
        {
            if (ReferenceEquals(child, splitter))
            {
                continue;
            }

            if (orientation == Avalonia.Layout.Orientation.Horizontal)
            {
                var column = Grid.GetColumn(child);
                if (column == 0)
                {
                    firstHost = child;
                }
                else if (column == 2)
                {
                    secondHost = child;
                }

                continue;
            }

            var row = Grid.GetRow(child);
            if (row == 0)
            {
                firstHost = child;
            }
            else if (row == 2)
            {
                secondHost = child;
            }
        }

        if (firstHost is null || secondHost is null)
        {
            return false;
        }

        firstBounds = GetSplitterEdgeBounds(
            firstHost,
            GetContentNode(firstHost),
            orientation,
            useTrailingEdge: true);
        secondBounds = GetSplitterEdgeBounds(
            secondHost,
            GetContentNode(secondHost),
            orientation,
            useTrailingEdge: false);
        return firstBounds.Width > 0
            && firstBounds.Height > 0
            && secondBounds.Width > 0
            && secondBounds.Height > 0;
    }

    private Rect GetSplitterEdgeBounds(
        Control host,
        EditorDockNodeViewModel? node,
        Avalonia.Layout.Orientation orientation,
        bool useTrailingEdge)
    {
        if (node is EditorDockSplitNodeViewModel split
            && split.Orientation == orientation
            && TryGetSplitChildHost(host, split, useTrailingEdge, out var edgeHost))
        {
            var edgeNode = useTrailingEdge ? split.Second : split.First;
            return GetSplitterEdgeBounds(edgeHost, edgeNode, orientation, useTrailingEdge);
        }

        return GetHostBounds(host);
    }

    private static bool TryGetSplitChildHost(
        Control host,
        EditorDockSplitNodeViewModel split,
        bool useSecondChild,
        out Control childHost)
    {
        childHost = null!;
        var splitView = host is EditorDockSplitNodeView directSplitView
            && ReferenceEquals(directSplitView.DataContext, split)
                ? directSplitView
                : host.GetVisualDescendants()
                    .OfType<EditorDockSplitNodeView>()
                    .FirstOrDefault(view => ReferenceEquals(view.DataContext, split));
        if (splitView is null)
        {
            return false;
        }

        var layoutSplitter = splitView.GetVisualDescendants()
            .OfType<GridSplitter>()
            .FirstOrDefault(candidate =>
                candidate.Classes.Contains("owned-dock-layout-splitter")
                && ReferenceEquals(candidate.DataContext, split));
        if (layoutSplitter?.Parent is not Grid grid)
        {
            return false;
        }

        var targetSlot = useSecondChild ? 2 : 0;
        foreach (var child in grid.Children.OfType<Control>())
        {
            if (split.Orientation == Avalonia.Layout.Orientation.Horizontal
                && Grid.GetColumn(child) == targetSlot)
            {
                childHost = child;
                return true;
            }

            if (split.Orientation == Avalonia.Layout.Orientation.Vertical
                && Grid.GetRow(child) == targetSlot)
            {
                childHost = child;
                return true;
            }
        }

        return false;
    }

    private static EditorDockNodeViewModel? GetContentNode(Control host)
    {
        return host is ContentControl { Content: EditorDockNodeViewModel node }
            ? node
            : host.DataContext as EditorDockNodeViewModel;
    }

    private Rect GetHostBounds(Control host)
    {
        var origin = host.TranslatePoint(new Point(0, 0), DockRoot) ?? default;
        return new Rect(origin, host.Bounds.Size);
    }

    private Dictionary<EditorDockTabViewModel, Rect> CaptureTabLayoutSnapshot()
    {
        var snapshot = new Dictionary<EditorDockTabViewModel, Rect>();
        foreach (var (tabHost, tab) in GetTabHostsForAnimation())
        {
            snapshot[tab] = GetHostBounds(tabHost);
        }

        return snapshot;
    }

    private void AnimateTabReorder(IReadOnlyDictionary<EditorDockTabViewModel, Rect> previousBounds)
    {
        DockLayout.UpdateLayout();
        InvalidateHitTestSnapshot();
        foreach (var (tabHost, tab) in GetTabHostsForAnimation())
        {
            if (!previousBounds.TryGetValue(tab, out var previousBoundsForTab))
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

            AnimateTabMove(tabHost, tab, deltaX, deltaY);
        }
    }

    private IEnumerable<(Control Host, EditorDockTabViewModel Tab)> GetTabHostsForAnimation()
    {
        foreach (var control in DockLayout.GetVisualDescendants().OfType<Control>())
        {
            if (!control.IsVisible
                || !control.Classes.Contains("owned-dock-tab")
                || !TryGetRealTab(control, out var tab))
            {
                continue;
            }

            yield return (control, tab);
        }
    }

    private void AnimateTabMove(
        Control tabHost,
        EditorDockTabViewModel tab,
        double deltaX,
        double deltaY)
    {
        var startX = deltaX;
        var startY = deltaY;
        if (tabMoveAnimations_.TryGetValue(tab, out var existingState))
        {
            startX += existingState.CurrentX;
            startY += existingState.CurrentY;
            StopTabMoveAnimation(tab, clearTransform: false);
        }

        if (Math.Abs(startX) < 0.5 && Math.Abs(startY) < 0.5)
        {
            return;
        }

        var transform = new TranslateTransform(startX, startY);
        tabHost.RenderTransform = transform;

        var state = new TabMoveAnimationState(
            tabHost,
            transform,
            DateTime.UtcNow,
            startX,
            startY);
        tabMoveAnimations_[tab] = state;
        if (!tabMoveAnimationTimer_.IsEnabled)
        {
            tabMoveAnimationTimer_.Start();
        }
    }

    private void OnTabMoveAnimationTick(object? sender, EventArgs e)
    {
        foreach (var (tab, state) in tabMoveAnimations_.ToArray())
        {
            var progress = Math.Clamp(
                (DateTime.UtcNow - state.StartedAt).TotalMilliseconds / TabReorderAnimationDuration.TotalMilliseconds,
                0d,
                1d);
            var eased = 1d - Math.Pow(1d - progress, 3d);
            state.CurrentX = state.StartX * (1d - eased);
            state.CurrentY = state.StartY * (1d - eased);
            state.Transform.X = state.CurrentX;
            state.Transform.Y = state.CurrentY;

            if (progress >= 1d)
            {
                StopTabMoveAnimation(tab);
            }
        }
    }

    private void StopTabMoveAnimations()
    {
        foreach (var tab in tabMoveAnimations_.Keys.ToArray())
        {
            StopTabMoveAnimation(tab);
        }
    }

    private void StopTabMoveAnimation(EditorDockTabViewModel tab, bool clearTransform = true)
    {
        if (!tabMoveAnimations_.Remove(tab, out var state))
        {
            return;
        }

        if (clearTransform)
        {
            state.Host.RenderTransform = null;
        }

        if (tabMoveAnimations_.Count == 0)
        {
            tabMoveAnimationTimer_.Stop();
        }
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
        return host.GetIdealTabBounds(DockRoot, window);
    }

    private static int? GetDragSourceTabIndex(EditorDockWindowViewModel window)
    {
        for (var index = 0; index < window.Tabs.Count; index++)
        {
            if (window.Tabs[index].IsDragSource)
            {
                return index;
            }
        }

        return null;
    }

    private static bool TryGetRealTab(Control tabHost, out EditorDockTabViewModel tab)
    {
        if (tabHost.DataContext is EditorDockTabViewModel directTab)
        {
            tab = directTab;
            return true;
        }

        if (tabHost.DataContext is EditorDockTabStripItemViewModel
            {
                IsPlaceholder: false,
                IsPreview: false,
                IsSourceGhost: false,
            } item)
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

    private bool ContainsTopLevelScreenPoint(PixelPoint screenPoint)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null || topLevel.Bounds.Width <= 0 || topLevel.Bounds.Height <= 0)
        {
            return false;
        }

        var scaling = topLevel.RenderScaling;
        var topLeft = topLevel.PointToScreen(new Point(0, 0));
        var right = topLeft.X + (topLevel.Bounds.Width * scaling);
        var bottom = topLeft.Y + (topLevel.Bounds.Height * scaling);
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

        workspace.HideDraggedDockTabPreview();
        workspace.InvalidateHitTestSnapshot();
        if (workspace.DataContext is EditorDockWorkspaceViewModel viewModel)
        {
            viewModel.ClearExternalDragPreview();
        }
    }

    private void BeginExternalDragPreview(
        EditorDockTabViewModel tab,
        Size previewSize,
        double pointerOffsetX)
    {
        draggedDockTabPreviewSize_ = previewSize;
        draggedDockTabPointerOffsetX_ = pointerOffsetX;
        InvalidateHitTestSnapshot();
        EnsureDraggedDockTabPreviewContent(tab);
        if (DataContext is EditorDockWorkspaceViewModel workspace)
        {
            workspace.BeginExternalDragPreview(tab);
        }
    }

    private void UpdateDraggedDockTabPreview(
        EditorDockWorkspaceViewModel workspace,
        Point point,
        EditorDockDropTarget target)
    {
        if (workspace.DragState.DraggedTab is not { } tab
            || target.Operation != EditorDockDropOperation.InsertTabAtIndex
            || draggedDockTabPreviewSize_.Width <= 0
            || draggedDockTabPreviewSize_.Height <= 0)
        {
            HideDraggedDockTabPreview();
            return;
        }

        EnsureDraggedDockTabPreviewContent(tab);
        DraggedDockTabPreview.Width = draggedDockTabPreviewSize_.Width;
        DraggedDockTabPreview.Height = target.PreviewBounds.Height > 0
            ? target.PreviewBounds.Height
            : draggedDockTabPreviewSize_.Height;
        Canvas.SetLeft(DraggedDockTabPreview, point.X - draggedDockTabPointerOffsetX_);
        Canvas.SetTop(DraggedDockTabPreview, target.PreviewBounds.Y);
        DraggedDockTabPreview.IsVisible = true;
    }

    private void EnsureDraggedDockTabPreviewContent(EditorDockTabViewModel tab)
    {
        if (DraggedDockTabPreview.Content is EditorDockTabStripItemViewModel item
            && ReferenceEquals(item.Tab, tab)
            && item.IsPreview)
        {
            return;
        }

        DraggedDockTabPreview.Content = new EditorDockTabStripItemViewModel(
            tab,
            isPlaceholder: false,
            isPreview: true);
    }

    private void HideDraggedDockTabPreview()
    {
        DraggedDockTabPreview.IsVisible = false;
        DraggedDockTabPreview.Content = null;
        DraggedDockTabPreview.Width = double.NaN;
        DraggedDockTabPreview.Height = double.NaN;
    }

    private void ClearPreviewWorkspace(EditorDockWorkspaceView? workspace)
    {
        if (workspace is null)
        {
            return;
        }

        if (ReferenceEquals(workspace, this))
        {
            HideDraggedDockTabPreview();
            InvalidateHitTestSnapshot();
            if (DataContext is EditorDockWorkspaceViewModel viewModel)
            {
                viewModel.ClearDropPreview();
            }

            return;
        }

        ClearExternalPreview(workspace);
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

    private static EditorDockWorkspaceView? FindWorkspaceTopLevelAtScreenPoint(PixelPoint screenPoint)
    {
        PruneWorkspaceReferences();
        for (var index = WorkspaceReferences.Count - 1; index >= 0; index--)
        {
            if (WorkspaceReferences[index].TryGetTarget(out var workspace)
                && workspace.ContainsTopLevelScreenPoint(screenPoint))
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

    private sealed record DockHitTestSnapshot(
        Size DockRootSize,
        Rect WorkspaceBounds,
        IReadOnlyList<EditorDockWindowBounds> Windows,
        IReadOnlyList<EditorDockSplitterBounds> Splitters);

    private sealed class TabMoveAnimationState
    {
        public TabMoveAnimationState(
            Control host,
            TranslateTransform transform,
            DateTime startedAt,
            double startX,
            double startY)
        {
            Host = host;
            Transform = transform;
            StartedAt = startedAt;
            StartX = startX;
            StartY = startY;
            CurrentX = startX;
            CurrentY = startY;
        }

        public Control Host { get; }

        public TranslateTransform Transform { get; }

        public DateTime StartedAt { get; }

        public double StartX { get; }

        public double StartY { get; }

        public double CurrentX { get; set; }

        public double CurrentY { get; set; }
    }
}
