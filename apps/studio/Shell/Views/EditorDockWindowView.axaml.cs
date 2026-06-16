using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockWindowView : UserControl
{
    private const double TabDragStartThreshold = 4.0;
    private const double TabReorderExitMargin = 8.0;
    private const double TabReorderSwitchHysteresis = 5.0;
    private const double TabReorderReverseSwitchThreshold = 18.0;
    private EditorDockTabViewModel? capturedTab_;
    private EditorDockWindowViewModel? capturedWindow_;
    private Point dragStartWorkspacePoint_;
    private Size draggedTabPreviewSize_;
    private double draggedTabPreviewTop_;
    private double draggedTabPointerOffsetX_;
    private double reorderLastSwitchCenterX_;
    private int reorderLastSwitchDirection_;
    private TabDragMode dragMode_;
    private int reorderTargetIndex_ = -1;
    private int reorderSourceIndex_ = -1;
    private int reorderTabCount_;
    private ReorderTabEntry[] reorderTabEntries_ = [];

    public EditorDockWindowView()
    {
        InitializeComponent();
        DockPanelContent.AddHandler(
            InputElement.PointerPressedEvent,
            OnPanelPointerPressed,
            RoutingStrategies.Bubble,
            handledEventsToo: true);
    }

    private void OnTabPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (e.Source is not Control source
            || source is Button
            || source.FindAncestorOfType<Button>() is not null
            || FindTabItemView(source) is not { DataContext: EditorDockTabStripItemViewModel { IsPlaceholder: false } item } tabItem
            || DataContext is not EditorDockWindowViewModel window)
        {
            return;
        }

        var tab = item.Tab;
        var point = e.GetCurrentPoint(tabItem);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        CaptureDraggedTabPreviewGeometry(tabItem, e);
        BeginTabInteraction(tab, window, e);
    }

    private void OnPanelPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (DataContext is not EditorDockWindowViewModel { ActiveTab: { } activeTab } window
            || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
        {
            return;
        }

        if (TryGetWorkspace(e, out var workspace, out _))
        {
            workspace.ActivateTab(activeTab);
            return;
        }

        window.Activate(activeTab);
    }

    private void BeginTabInteraction(
        EditorDockTabViewModel tab,
        EditorDockWindowViewModel window,
        PointerPressedEventArgs e)
    {
        if (TryGetWorkspace(e, out var workspace, out var workspacePoint))
        {
            dragStartWorkspacePoint_ = workspacePoint;
            workspace.ActivateTab(tab);
        }
        else
        {
            window.Activate(tab);
        }

        capturedTab_ = tab;
        capturedWindow_ = window;
        dragMode_ = TabDragMode.Pending;
        reorderTargetIndex_ = -1;
        e.Pointer.Capture(this);

        e.Handled = true;
    }

    private static EditorDockTabItemView? FindTabItemView(Control source)
    {
        if (source is EditorDockTabItemView tabItem)
        {
            return tabItem;
        }

        return source.FindAncestorOfType<EditorDockTabItemView>();
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        if (capturedTab_ is null)
        {
            return;
        }

        if (!TryGetWorkspace(e, out var workspace, out var workspacePoint))
        {
            e.Handled = true;
            return;
        }

        if (dragMode_ == TabDragMode.Pending)
        {
            if (!HasMovedPastDragThreshold(dragStartWorkspacePoint_, workspacePoint))
            {
                e.Handled = true;
                return;
            }

            if (IsPointerOutsideWorkspace(workspace, workspacePoint))
            {
                BeginDockTabDrag(workspace, workspacePoint);
            }
            else if (IsPointerInSourceTabStrip(e, TabReorderExitMargin))
            {
                BeginLocalTabReorder(workspace, e);
            }
            else
            {
                BeginDockTabDrag(workspace, workspacePoint);
            }
        }
        else if (dragMode_ == TabDragMode.Reorder)
        {
            if (IsPointerOutsideWorkspace(workspace, workspacePoint))
            {
                CancelLocalTabReorder(workspace);
                BeginDockTabDrag(workspace, workspacePoint);
            }
            else if (IsPointerInSourceTabStrip(e, TabReorderExitMargin))
            {
                UpdateLocalTabReorder(workspace, e);
            }
            else
            {
                CancelLocalTabReorder(workspace);
                BeginDockTabDrag(workspace, workspacePoint);
            }
        }
        else if (dragMode_ == TabDragMode.Dock
            && !IsPointerOutsideWorkspace(workspace, workspacePoint)
            && IsPointerInSourceTabStrip(e, TabReorderExitMargin))
        {
            workspace.CancelTabDrag();
            BeginLocalTabReorder(workspace, e);
        }

        if (dragMode_ == TabDragMode.Dock)
        {
            workspace.UpdateTabDrag(workspacePoint);
        }

        e.Handled = true;
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        if (capturedTab_ is null)
        {
            return;
        }

        if (dragMode_ == TabDragMode.Dock
            && TryGetWorkspace(e, out var workspace, out var workspacePoint))
        {
            workspace.CompleteTabDrag(workspacePoint);
        }
        else if (dragMode_ == TabDragMode.Reorder
            && TryGetWorkspace(e, out workspace, out _)
            && capturedWindow_ is not null)
        {
            workspace.CompleteTabReorder(capturedWindow_, capturedTab_, reorderTargetIndex_);
            capturedTab_.SetDragSourceState(false);
        }

        ClearCapture(e.Pointer);
        e.Handled = true;
    }

    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e)
    {
        base.OnPointerCaptureLost(e);
        if (capturedTab_ is null)
        {
            return;
        }

        if (dragMode_ == TabDragMode.Dock
            && TryGetWorkspace(e, out var workspace, out _))
        {
            workspace.CancelTabDrag();
        }
        else if (dragMode_ == TabDragMode.Reorder
            && TryGetWorkspace(e, out workspace, out _))
        {
            CancelLocalTabReorder(workspace);
        }

        ClearInteractionState();
    }

    private void BeginLocalTabReorder(EditorDockWorkspaceView workspace, PointerEventArgs e)
    {
        if (capturedWindow_ is null || capturedTab_ is null)
        {
            return;
        }

        dragMode_ = TabDragMode.Reorder;
        reorderSourceIndex_ = capturedWindow_.Tabs.IndexOf(capturedTab_);
        if (reorderSourceIndex_ < 0)
        {
            dragMode_ = TabDragMode.None;
            return;
        }

        reorderTabCount_ = capturedWindow_.Tabs.Count;
        reorderTabEntries_ = CaptureReorderTabEntries(capturedWindow_, this);
        capturedTab_.SetDragSourceState(true);
        ShowDraggedTabPreview(e);
        reorderLastSwitchCenterX_ = e.GetPosition(this).X
            - draggedTabPointerOffsetX_
            + (draggedTabPreviewSize_.Width / 2);
        reorderLastSwitchDirection_ = 0;
        UpdateLocalTabReorder(workspace, e);
    }

    private void UpdateLocalTabReorder(EditorDockWorkspaceView workspace, PointerEventArgs e)
    {
        if (capturedWindow_ is null || capturedTab_ is null)
        {
            return;
        }

        var pointer = e.GetPosition(this);
        UpdateDraggedTabPreview(pointer);
        var draggedTabCenterX = pointer.X
            - draggedTabPointerOffsetX_
            + (draggedTabPreviewSize_.Width / 2);
        var previousTargetIndex = reorderTargetIndex_;
        var proposedTargetIndex = ResolveTabReorderTargetIndex(
            draggedTabCenterX,
            capturedTab_,
            reorderTargetIndex_);
        reorderTargetIndex_ = ApplyReorderTargetHysteresis(
            draggedTabCenterX,
            reorderTargetIndex_,
            proposedTargetIndex);
        if (previousTargetIndex >= 0 && reorderTargetIndex_ != previousTargetIndex)
        {
            reorderLastSwitchDirection_ = System.Math.Sign(reorderTargetIndex_ - previousTargetIndex);
            reorderLastSwitchCenterX_ = draggedTabCenterX;
        }

        if (reorderTargetIndex_ != previousTargetIndex)
        {
            workspace.PreviewTabReorder(capturedWindow_, capturedTab_, reorderTargetIndex_);
        }
    }

    private void CancelLocalTabReorder(EditorDockWorkspaceView workspace)
    {
        if (capturedWindow_ is not null)
        {
            workspace.CancelTabReorder(capturedWindow_);
        }

        capturedTab_?.SetDragSourceState(false);
        reorderTargetIndex_ = -1;
        reorderSourceIndex_ = -1;
        reorderTabCount_ = 0;
        reorderTabEntries_ = [];
        HideDraggedTabPreview();
    }

    private void BeginDockTabDrag(EditorDockWorkspaceView workspace, Point workspacePoint)
    {
        if (capturedWindow_ is null || capturedTab_ is null)
        {
            return;
        }

        dragMode_ = TabDragMode.Dock;
        capturedWindow_.HideDragSourceTab(capturedTab_);
        workspace.BeginTabDrag(
            capturedTab_,
            workspacePoint,
            draggedTabPreviewSize_,
            draggedTabPointerOffsetX_);
    }

    private bool TryGetWorkspace(RoutedEventArgs args, out EditorDockWorkspaceView workspace, out Point point)
    {
        var ancestor = this.FindAncestorOfType<EditorDockWorkspaceView>();
        if (ancestor is null)
        {
            workspace = null!;
            point = default;
            return false;
        }

        workspace = ancestor;
        point = args switch
        {
            PointerEventArgs pointerArgs => pointerArgs.GetPosition(workspace),
            _ => default,
        };

        return true;
    }

    private void ClearCapture(IPointer pointer)
    {
        ClearInteractionState();
        pointer.Capture(null);
    }

    private void ClearInteractionState()
    {
        HideDraggedTabPreview();
        capturedWindow_?.ClearHiddenDragSourceTab();
        capturedTab_ = null;
        capturedWindow_ = null;
        draggedTabPreviewSize_ = default;
        draggedTabPreviewTop_ = 0;
        draggedTabPointerOffsetX_ = 0;
        reorderLastSwitchCenterX_ = 0;
        reorderLastSwitchDirection_ = 0;
        dragMode_ = TabDragMode.None;
        reorderTargetIndex_ = -1;
        reorderSourceIndex_ = -1;
        reorderTabCount_ = 0;
        reorderTabEntries_ = [];
    }

    private void CaptureDraggedTabPreviewGeometry(EditorDockTabItemView tabItem, PointerEventArgs e)
    {
        var origin = tabItem.TranslatePoint(new Point(0, 0), this) ?? default;
        var pointer = e.GetPosition(this);
        draggedTabPreviewSize_ = tabItem.Bounds.Size;
        draggedTabPreviewTop_ = origin.Y;
        draggedTabPointerOffsetX_ = pointer.X - origin.X;
    }

    private void ShowDraggedTabPreview(PointerEventArgs e)
    {
        if (capturedTab_ is null)
        {
            return;
        }

        DraggedTabPreview.Content = new EditorDockTabStripItemViewModel(
            capturedTab_,
            isPlaceholder: false,
            isPreview: true);
        DraggedTabPreview.Width = draggedTabPreviewSize_.Width;
        DraggedTabPreview.Height = draggedTabPreviewSize_.Height;
        DraggedTabPreview.IsVisible = true;
        UpdateDraggedTabPreview(e.GetPosition(this));
    }

    private void UpdateDraggedTabPreview(Point pointer)
    {
        if (!DraggedTabPreview.IsVisible)
        {
            return;
        }

        Canvas.SetLeft(DraggedTabPreview, pointer.X - draggedTabPointerOffsetX_);
        Canvas.SetTop(DraggedTabPreview, draggedTabPreviewTop_);
    }

    private void HideDraggedTabPreview()
    {
        DraggedTabPreview.IsVisible = false;
        DraggedTabPreview.Content = null;
        DraggedTabPreview.Width = double.NaN;
        DraggedTabPreview.Height = double.NaN;
    }

    private static bool HasMovedPastDragThreshold(Point start, Point current)
    {
        var dx = current.X - start.X;
        var dy = current.Y - start.Y;
        return (dx * dx) + (dy * dy) >= TabDragStartThreshold * TabDragStartThreshold;
    }

    private static bool IsPointerOutsideWorkspace(EditorDockWorkspaceView workspace, Point pointer)
    {
        return pointer.X < 0
            || pointer.X > workspace.Bounds.Width
            || pointer.Y < 0
            || pointer.Y > workspace.Bounds.Height;
    }

    private bool IsPointerInSourceTabStrip(PointerEventArgs e, double verticalMargin)
    {
        if (!TryGetTabStripBounds(out var bounds))
        {
            return false;
        }

        return Inflate(bounds, 0, verticalMargin).Contains(e.GetPosition(this));
    }

    private bool TryGetTabStripBounds(out Rect bounds)
    {
        if (DockTabStrip.Bounds.Width <= 0 || DockTabStrip.Bounds.Height <= 0)
        {
            bounds = default;
            return false;
        }

        var origin = DockTabStrip.TranslatePoint(new Point(0, 0), this);
        if (origin is null)
        {
            bounds = default;
            return false;
        }

        bounds = new Rect(origin.Value, DockTabStrip.Bounds.Size);
        return true;
    }

    private int ResolveTabReorderTargetIndex(
        double draggedTabCenterX,
        EditorDockTabViewModel draggedTab,
        int currentTargetIndex)
    {
        if (reorderSourceIndex_ < 0)
        {
            return currentTargetIndex >= 0 ? currentTargetIndex : 0;
        }

        if (reorderTabEntries_.Length == 0)
        {
            return reorderSourceIndex_;
        }

        var targetIndex = currentTargetIndex >= 0 ? currentTargetIndex : reorderSourceIndex_;
        while (TryResolveAdjacentReorderTargetIndex(
                   draggedTabCenterX,
                   draggedTab,
                   targetIndex,
                   out var nextTargetIndex)
               && nextTargetIndex != targetIndex)
        {
            targetIndex = nextTargetIndex;
        }

        return targetIndex;
    }

    private bool TryResolveAdjacentReorderTargetIndex(
        double draggedTabCenterX,
        EditorDockTabViewModel draggedTab,
        int targetIndex,
        out int nextTargetIndex)
    {
        nextTargetIndex = targetIndex;
        var clampedTargetIndex = System.Math.Clamp(targetIndex, 0, reorderTabCount_);
        var currentX = reorderTabEntries_[0].Bounds.X;
        var placeholderWidth = ResolveReorderPlaceholderWidth(draggedTab);
        var placeholderCenterX = 0d;
        var hasPlaceholder = false;
        var hasPrevious = false;
        var previousCenterX = 0d;
        var previousTabIndex = 0;
        var hasNext = false;
        var nextCenterX = 0d;
        var nextTabIndex = 0;

        for (var tabIndex = 0; tabIndex <= reorderTabCount_; tabIndex++)
        {
            if (clampedTargetIndex == tabIndex)
            {
                placeholderCenterX = currentX + (placeholderWidth / 2);
                hasPlaceholder = true;
                currentX += placeholderWidth;
            }

            if (tabIndex >= reorderTabCount_
                || !TryGetReorderTabEntry(tabIndex, out var entry)
                || ReferenceEquals(entry.Tab, draggedTab))
            {
                continue;
            }

            var entryWidth = entry.Bounds.Width > 0 ? entry.Bounds.Width : placeholderWidth;
            var entryCenterX = currentX + (entryWidth / 2);
            currentX += entryWidth;

            if (hasPlaceholder && !hasNext)
            {
                hasNext = true;
                nextCenterX = entryCenterX;
                nextTabIndex = entry.TabIndex;
                continue;
            }

            if (!hasPlaceholder)
            {
                hasPrevious = true;
                previousCenterX = entryCenterX;
                previousTabIndex = entry.TabIndex;
            }
        }

        if (!hasPlaceholder)
        {
            return false;
        }

        if (hasPrevious)
        {
            var leftBoundary = (previousCenterX + placeholderCenterX) / 2;
            if (draggedTabCenterX < leftBoundary - TabReorderSwitchHysteresis)
            {
                nextTargetIndex = previousTabIndex;
                return true;
            }
        }

        if (hasNext)
        {
            var rightBoundary = (placeholderCenterX + nextCenterX) / 2;
            if (draggedTabCenterX >= rightBoundary + TabReorderSwitchHysteresis)
            {
                nextTargetIndex = nextTabIndex + 1;
                return true;
            }
        }

        return false;
    }

    private double ResolveReorderPlaceholderWidth(EditorDockTabViewModel draggedTab)
    {
        if (TryGetReorderTabEntry(reorderSourceIndex_, out var sourceEntry)
            && ReferenceEquals(sourceEntry.Tab, draggedTab)
            && sourceEntry.Bounds.Width > 0)
        {
            return sourceEntry.Bounds.Width;
        }

        return draggedTabPreviewSize_.Width > 0 ? draggedTabPreviewSize_.Width : 1d;
    }

    private bool TryGetReorderTabEntry(int tabIndex, out ReorderTabEntry entry)
    {
        if (tabIndex >= 0
            && tabIndex < reorderTabEntries_.Length
            && reorderTabEntries_[tabIndex].TabIndex == tabIndex)
        {
            entry = reorderTabEntries_[tabIndex];
            return true;
        }

        foreach (var candidate in reorderTabEntries_)
        {
            if (candidate.TabIndex == tabIndex)
            {
                entry = candidate;
                return true;
            }
        }

        entry = default;
        return false;
    }

    private int ApplyReorderTargetHysteresis(
        double draggedTabCenterX,
        int currentTargetIndex,
        int proposedTargetIndex)
    {
        if (currentTargetIndex < 0 || proposedTargetIndex == currentTargetIndex)
        {
            return proposedTargetIndex;
        }

        var proposedDirection = System.Math.Sign(proposedTargetIndex - currentTargetIndex);
        if (reorderLastSwitchDirection_ != 0
            && proposedDirection != 0
            && proposedDirection != reorderLastSwitchDirection_
            && System.Math.Abs(draggedTabCenterX - reorderLastSwitchCenterX_) < TabReorderReverseSwitchThreshold)
        {
            return currentTargetIndex;
        }

        return proposedTargetIndex;
    }

    internal IReadOnlyList<Editor.Shell.Docking.EditorDockTabBounds> GetIdealTabBounds(
        Visual relativeTo,
        EditorDockWindowViewModel window)
    {
        var tabBounds = new List<Editor.Shell.Docking.EditorDockTabBounds>();
        var tabIndices = CreateTabIndexMap(window);
        foreach (var entry in GetIdealTabStripEntries(window, relativeTo))
        {
            if (entry.Item.IsPlaceholder
                || entry.Item.IsPreview
                || entry.Item.IsSourceGhost)
            {
                continue;
            }

            if (!tabIndices.TryGetValue(entry.Item.Tab, out var tabIndex)
                || entry.Bounds.Width <= 0
                || entry.Bounds.Height <= 0)
            {
                continue;
            }

            tabBounds.Add(new Editor.Shell.Docking.EditorDockTabBounds(
                entry.Item.Tab.Id,
                tabIndex,
                entry.Bounds,
                entry.Item.Tab.IsDragSource));
        }

        tabBounds.Sort((left, right) => left.TabIndex.CompareTo(right.TabIndex));
        return tabBounds;
    }

    private ReorderTabEntry[] CaptureReorderTabEntries(
        EditorDockWindowViewModel window,
        Visual relativeTo)
    {
        var entries = new List<ReorderTabEntry>();
        var tabIndices = CreateTabIndexMap(window);
        foreach (var entry in GetIdealTabStripEntries(window, relativeTo))
        {
            if (entry.Item.IsPlaceholder
                || entry.Item.IsPreview
                || entry.Item.IsSourceGhost)
            {
                continue;
            }

            if (!tabIndices.TryGetValue(entry.Item.Tab, out var tabIndex)
                || entry.Bounds.Width <= 0
                || entry.Bounds.Height <= 0)
            {
                continue;
            }

            entries.Add(new ReorderTabEntry(entry.Item.Tab, tabIndex, entry.Bounds));
        }

        entries.Sort((left, right) => left.TabIndex.CompareTo(right.TabIndex));
        return entries.ToArray();
    }

    private static Dictionary<EditorDockTabViewModel, int> CreateTabIndexMap(EditorDockWindowViewModel window)
    {
        var tabIndices = new Dictionary<EditorDockTabViewModel, int>(window.Tabs.Count);
        for (var index = 0; index < window.Tabs.Count; index++)
        {
            tabIndices[window.Tabs[index]] = index;
        }

        return tabIndices;
    }

    private List<IdealTabStripEntry> GetIdealTabStripEntries(
        EditorDockWindowViewModel window,
        Visual relativeTo)
    {
        if (DockTabStrip.TranslatePoint(new Point(0, 0), relativeTo) is not { } origin)
        {
            return [];
        }

        var sizesByTab = new Dictionary<EditorDockTabViewModel, Size>();
        foreach (var visual in DockTabStrip.GetVisualDescendants())
        {
            if (visual is not EditorDockTabItemView tabHost
                || !tabHost.IsVisible
                || tabHost.DataContext is not EditorDockTabStripItemViewModel item)
            {
                continue;
            }

            var width = tabHost.Bounds.Width;
            var height = tabHost.Bounds.Height;
            if (width <= 0 || height <= 0)
            {
                continue;
            }

            sizesByTab[item.Tab] = tabHost.Bounds.Size;
        }

        var fallbackWidth = ResolveIdealTabFallbackWidth(sizesByTab.Values);
        var fallbackHeight = DockTabStrip.Bounds.Height > 0
            ? DockTabStrip.Bounds.Height
            : draggedTabPreviewSize_.Height > 0
                ? draggedTabPreviewSize_.Height
                : 1d;
        var currentX = origin.X;
        var entries = new List<IdealTabStripEntry>(window.TabStripItems.Count);
        foreach (var item in window.TabStripItems)
        {
            var size = sizesByTab.TryGetValue(item.Tab, out var measuredSize)
                ? measuredSize
                : new Size(fallbackWidth, fallbackHeight);
            var bounds = new Rect(currentX, origin.Y, size.Width, size.Height);
            entries.Add(new IdealTabStripEntry(item, bounds));
            currentX += size.Width;
        }

        return entries;
    }

    private double ResolveIdealTabFallbackWidth(ICollection<Size> measuredSizes)
    {
        if (draggedTabPreviewSize_.Width > 0)
        {
            return draggedTabPreviewSize_.Width;
        }

        if (measuredSizes.Count == 0)
        {
            return 1d;
        }

        var totalWidth = 0d;
        foreach (var size in measuredSizes)
        {
            totalWidth += size.Width;
        }

        return totalWidth / measuredSizes.Count;
    }

    private static Rect Inflate(Rect rect, double x, double y)
    {
        return new Rect(
            rect.X - x,
            rect.Y - y,
            rect.Width + (x * 2),
            rect.Height + (y * 2));
    }

    private enum TabDragMode
    {
        None,
        Pending,
        Reorder,
        Dock,
    }

    private readonly record struct IdealTabStripEntry(
        EditorDockTabStripItemViewModel Item,
        Rect Bounds);

    private readonly record struct ReorderTabEntry(
        EditorDockTabViewModel Tab,
        int TabIndex,
        Rect Bounds);
}
