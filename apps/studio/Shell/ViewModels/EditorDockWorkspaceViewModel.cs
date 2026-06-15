using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Layout;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Docking;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockWorkspaceViewModel : ViewModelBase
{
    private const string DynamicWindowIdPrefix = "owned-dock-window-";
    private readonly Dictionary<DockArea, EditorDockWindowViewModel> windowsByArea_;
    private readonly Dictionary<string, EditorDockWindowViewModel> windowsById_;
    private EditorDockNodeViewModel? rootNode_;
    private EditorDockWindowViewModel? activeWindow_;
    private EditorDockWindowViewModel? dragSourceWindow_;
    private EditorDockTabViewModel? dragSourceTab_;
    private EditorDockWindowViewModel? tabInsertPreviewWindow_;
    private int nextDynamicWindowIndex_ = 1;
    private int nextDynamicSplitIndex_ = 1;

    public EditorDockWorkspaceViewModel(IPanelRegistry panelRegistry)
    {
        WorkspaceKind = EditorDockWorkspaceKind.MainWindow;
        LeftWindow = new EditorDockWindowViewModel("owned-dock-left", "Hierarchy", DockArea.Left, "Scene tree");
        CenterWindow = new EditorDockWindowViewModel("owned-dock-center", "Viewport", DockArea.Center, "Primary work area");
        BottomWindow = new EditorDockWindowViewModel("owned-dock-bottom", "Diagnostics", DockArea.Bottom, "Output and validation");
        RightWindow = new EditorDockWindowViewModel("owned-dock-right", "Inspector", DockArea.Right, "Selection context");
        rootNode_ = CreateDefaultLayout();

        windowsByArea_ = new Dictionary<DockArea, EditorDockWindowViewModel>
        {
            [DockArea.Left] = LeftWindow,
            [DockArea.Center] = CenterWindow,
            [DockArea.Bottom] = BottomWindow,
            [DockArea.Right] = RightWindow,
        };
        windowsById_ = new Dictionary<string, EditorDockWindowViewModel>
        {
            [LeftWindow.Id] = LeftWindow,
            [CenterWindow.Id] = CenterWindow,
            [BottomWindow.Id] = BottomWindow,
            [RightWindow.Id] = RightWindow,
        };

        foreach (var descriptor in panelRegistry.GetAll())
        {
            var window = windowsByArea_[descriptor.DefaultArea];
            window.Add(CreateTab(descriptor));
        }

        SetActiveWindow(CenterWindow.Tabs.Count > 0 ? CenterWindow : FindFirstWindowWithContent());
    }

    private EditorDockWorkspaceViewModel(EditorDockWindowViewModel floatingDockWindow)
    {
        WorkspaceKind = EditorDockWorkspaceKind.FloatingWindow;
        LeftWindow = floatingDockWindow;
        CenterWindow = floatingDockWindow;
        BottomWindow = floatingDockWindow;
        RightWindow = floatingDockWindow;
        windowsByArea_ = [];
        windowsById_ = new Dictionary<string, EditorDockWindowViewModel>
        {
            [floatingDockWindow.Id] = floatingDockWindow,
        };
        nextDynamicWindowIndex_ = GetNextDynamicWindowIndex(windowsById_.Values);
        rootNode_ = new EditorDockWindowNodeViewModel($"node-{floatingDockWindow.Id}", floatingDockWindow);
        SetActiveWindow(floatingDockWindow);
    }

    public EditorDockWindowViewModel LeftWindow { get; }

    public EditorDockWindowViewModel CenterWindow { get; }

    public EditorDockWindowViewModel BottomWindow { get; }

    public EditorDockWindowViewModel RightWindow { get; }

    public EditorDockWorkspaceKind WorkspaceKind { get; }

    public bool IsMainWindow => WorkspaceKind == EditorDockWorkspaceKind.MainWindow;

    public bool IsFloatingWindow => WorkspaceKind == EditorDockWorkspaceKind.FloatingWindow;

    public string WorkspaceKindText => IsFloatingWindow ? "Floating Window" : "Main Window";

    public EditorDockWindowViewModel? ActiveWindow => activeWindow_;

    public string ActiveWindowTitle => ActiveWindow?.Title ?? "No active window";

    public string HostTitle => IsFloatingWindow
        ? $"{ActiveWindowTitle} - Floating Window"
        : ActiveWindowTitle;

    public EditorDockNodeViewModel? RootNode
    {
        get => rootNode_;
        private set
        {
            if (SetProperty(ref rootNode_, value))
            {
                OnPropertyChanged(nameof(HasRootNode));
            }
        }
    }

    public bool HasRootNode => RootNode is not null;

    public EditorDockDragStateViewModel DragState { get; } = new();

    public void BeginDrag(EditorDockTabViewModel tab, double x, double y)
    {
        var window = FindWindow(tab);
        if (window is null)
        {
            return;
        }

        window.Activate(tab);
        SetActiveWindow(window);
        SetDragSourceState(window, tab);
        DragState.Begin(tab, x, y);
    }

    public void UpdateDrag(double x, double y)
    {
        DragState.UpdatePointer(x, y);
    }

    public void BeginExternalDragPreview(EditorDockTabViewModel tab, double x, double y)
    {
        if (!DragState.IsActive || !ReferenceEquals(DragState.DraggedTab, tab))
        {
            DragState.Begin(tab, x, y);
            return;
        }

        DragState.UpdatePointer(x, y);
    }

    public void ClearDropPreview()
    {
        ClearTabInsertPreview();
        DragState.ClearDropPreview();
    }

    public void ClearExternalDragPreview()
    {
        ClearTabInsertPreview();
        DragState.Clear();
    }

    public bool PreviewTabInsert(EditorDockDropTarget target)
    {
        var tab = DragState.DraggedTab;
        if (tab is null
            || target.Operation != EditorDockDropOperation.InsertTabAtIndex
            || target.TargetId is not { } targetWindowId
            || target.TargetIndex is not { } targetIndex)
        {
            return ClearTabInsertPreview();
        }

        if (!windowsById_.TryGetValue(targetWindowId, out var targetWindow))
        {
            return ClearTabInsertPreview();
        }

        var changed = false;
        if (!ReferenceEquals(tabInsertPreviewWindow_, targetWindow))
        {
            changed = ClearTabInsertPreview();
            tabInsertPreviewWindow_ = targetWindow;
        }

        return targetWindow.ShowTabInsertPlaceholder(tab, targetIndex) || changed;
    }

    public EditorDockFloatingWindowRequest? CompleteDrag(EditorDockDropTarget target)
    {
        var tab = DragState.DraggedTab;
        try
        {
            if (tab is null)
            {
                return null;
            }

            EditorDockFloatingWindowRequest? request = null;
            if (target.Operation == EditorDockDropOperation.TabInto
                && target.TargetId is { } targetWindowId
                && windowsById_.TryGetValue(targetWindowId, out var targetWindow))
            {
                MoveTab(tab, targetWindow);
            }
            else if (target.Operation == EditorDockDropOperation.InsertTabAtIndex
                && target.TargetId is { } tabInsertTargetWindowId
                && target.TargetIndex is { } tabInsertTargetIndex)
            {
                InsertTabAtIndex(tab, tabInsertTargetWindowId, tabInsertTargetIndex);
            }
            else if (target.Operation == EditorDockDropOperation.SplitBetween
                && target.TargetId is { } targetSplitId)
            {
                InsertTabAtSplitter(tab, targetSplitId);
            }
            else if (IsWindowInsertOperation(target.Operation)
                && target.TargetId is { } insertTargetWindowId)
            {
                InsertTabAdjacentToWindow(tab, insertTargetWindowId, target.Operation);
            }
            else if (IsWorkspaceEdgeInsertOperation(target.Operation))
            {
                InsertTabAtWorkspaceEdge(tab, target.Operation);
            }
            else if (target.Operation == EditorDockDropOperation.Float)
            {
                request = FloatTab(tab, target.PreviewBounds);
            }

            return request;
        }
        finally
        {
            ClearTabInsertPreview();
            ClearDragSourceState();
            DragState.Clear();
        }
    }

    public EditorDockFloatingWindowRequest? CompleteDragInto(
        EditorDockWorkspaceViewModel targetWorkspace,
        EditorDockDropTarget target)
    {
        if (ReferenceEquals(this, targetWorkspace))
        {
            return CompleteDrag(target);
        }

        try
        {
            var tab = DragState.DraggedTab;
            if (tab is null)
            {
                return null;
            }

            var sourceWindow = FindWindow(tab);
            if (sourceWindow is null || !targetWorkspace.CanAcceptDetachedTab(target))
            {
                return null;
            }

            sourceWindow.Remove(tab);
            targetWorkspace.InsertDetachedTab(tab, target, sourceWindow.Area);
            RemoveWindowIfEmpty(sourceWindow);
            NormalizeLayoutGraph();
            SetActiveWindow(sourceWindow.Tabs.Count > 0 ? sourceWindow : FindFirstWindowWithContent());
            return null;
        }
        finally
        {
            ClearTabInsertPreview();
            ClearDragSourceState();
            DragState.Clear();
            targetWorkspace.ClearExternalDragPreview();
        }
    }

    public void CancelDrag()
    {
        ClearTabInsertPreview();
        ClearDragSourceState();
        DragState.Clear();
    }

    public bool HasDockContent()
    {
        foreach (var window in windowsById_.Values)
        {
            if (window.Tabs.Count > 0)
            {
                return true;
            }
        }

        return false;
    }

    private void MoveTab(EditorDockTabViewModel tab, EditorDockWindowViewModel targetWindow)
    {
        var sourceWindow = FindWindow(tab);
        if (sourceWindow is null)
        {
            return;
        }

        if (!ReferenceEquals(sourceWindow, targetWindow))
        {
            sourceWindow.Remove(tab);
            targetWindow.Add(tab);
        }

        targetWindow.Activate(tab);
        SetActiveWindow(targetWindow);
    }

    private void InsertTabAtIndex(
        EditorDockTabViewModel tab,
        string targetWindowId,
        int targetIndex)
    {
        var sourceWindow = FindWindow(tab);
        if (sourceWindow is null || !windowsById_.TryGetValue(targetWindowId, out var targetWindow))
        {
            return;
        }

        if (ReferenceEquals(sourceWindow, targetWindow))
        {
            ReorderTabInWindow(tab, targetWindow, targetIndex);
            return;
        }

        sourceWindow.Remove(tab);
        targetWindow.Insert(tab, targetIndex);
        targetWindow.Activate(tab);
        SetActiveWindow(targetWindow);
        RemoveWindowIfEmpty(sourceWindow);
        NormalizeLayoutGraph();
    }

    private void ReorderTabInWindow(
        EditorDockTabViewModel tab,
        EditorDockWindowViewModel targetWindow,
        int targetIndex)
    {
        var sourceIndex = targetWindow.Tabs.IndexOf(tab);
        if (sourceIndex < 0)
        {
            return;
        }

        targetWindow.Move(tab, targetIndex);
        targetWindow.Activate(tab);
        SetActiveWindow(targetWindow);
    }

    private EditorDockWindowViewModel? FindWindow(EditorDockTabViewModel tab)
    {
        foreach (var window in windowsById_.Values)
        {
            if (window.Tabs.Contains(tab))
            {
                return window;
            }
        }

        return null;
    }

    private EditorDockWindowViewModel? FindFirstWindowWithContent()
    {
        foreach (var window in windowsById_.Values)
        {
            if (window.Tabs.Count > 0)
            {
                return window;
            }
        }

        return null;
    }

    private void SetActiveWindow(EditorDockWindowViewModel? window)
    {
        if (ReferenceEquals(activeWindow_, window))
        {
            return;
        }

        activeWindow_?.SetActiveWindowState(false);
        activeWindow_ = window;
        activeWindow_?.SetActiveWindowState(true);
        OnPropertyChanged(nameof(ActiveWindow));
        OnPropertyChanged(nameof(ActiveWindowTitle));
        OnPropertyChanged(nameof(HostTitle));
    }

    private void SetDragSourceState(EditorDockWindowViewModel window, EditorDockTabViewModel tab)
    {
        ClearDragSourceState();
        dragSourceWindow_ = window;
        dragSourceTab_ = tab;
        dragSourceWindow_.SetDragSourceWindowState(true);
        dragSourceTab_.SetDragSourceState(true);
    }

    private void ClearDragSourceState()
    {
        dragSourceWindow_?.SetDragSourceWindowState(false);
        dragSourceTab_?.SetDragSourceState(false);
        dragSourceWindow_ = null;
        dragSourceTab_ = null;
    }

    private bool ClearTabInsertPreview()
    {
        if (tabInsertPreviewWindow_ is null)
        {
            return false;
        }

        var changed = tabInsertPreviewWindow_.ClearTabInsertPlaceholder();
        tabInsertPreviewWindow_ = null;
        return changed;
    }

    private EditorDockFloatingWindowRequest? FloatTab(EditorDockTabViewModel tab, Avalonia.Rect bounds)
    {
        var sourceWindow = FindWindow(tab);
        if (sourceWindow is null)
        {
            return null;
        }

        sourceWindow.Remove(tab);

        var floatingDockWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        floatingDockWindow.Add(tab);
        RemoveWindowIfEmpty(sourceWindow);
        NormalizeLayoutGraph();

        var floatingWorkspace = new EditorDockWorkspaceViewModel(floatingDockWindow);
        var floatingWindow = new EditorDockFloatingWindowViewModel(floatingWorkspace);
        return new EditorDockFloatingWindowRequest(floatingWindow, bounds);
    }

    private void InsertTabAtSplitter(EditorDockTabViewModel tab, string splitId)
    {
        var sourceWindow = FindWindow(tab);
        var targetSplit = FindSplitNode(RootNode, splitId);
        if (sourceWindow is null || targetSplit is null)
        {
            return;
        }

        sourceWindow.Remove(tab);

        var insertedWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        insertedWindow.Add(tab);
        windowsById_.Add(insertedWindow.Id, insertedWindow);

        var insertedNode = new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
        InsertWindowNodeAtSplitter(targetSplit, insertedNode);

        RemoveWindowIfEmpty(sourceWindow);
        NormalizeLayoutGraph();
        SetActiveWindow(insertedWindow);
    }

    private void InsertTabAdjacentToWindow(
        EditorDockTabViewModel tab,
        string targetWindowId,
        EditorDockDropOperation operation)
    {
        var sourceWindow = FindWindow(tab);
        if (sourceWindow is null)
        {
            return;
        }

        if (!TryFindWindowNode(
                RootNode,
                targetWindowId,
                parent: null,
                out _,
                out _,
                out var targetNode)
            || targetNode is null)
        {
            return;
        }

        if (ReferenceEquals(sourceWindow, targetNode.Window) && sourceWindow.Tabs.Count == 1)
        {
            return;
        }

        sourceWindow.Remove(tab);

        var insertedWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        insertedWindow.Add(tab);
        windowsById_.Add(insertedWindow.Id, insertedWindow);

        var insertedNode = new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
        var replacement = CreateWindowInsertionSplit(operation, targetNode, insertedNode);

        ReplaceNode(targetNode, replacement);
        RemoveWindowIfEmpty(sourceWindow);
        NormalizeLayoutGraph();
        SetActiveWindow(insertedWindow);
    }

    private void InsertTabAtWorkspaceEdge(
        EditorDockTabViewModel tab,
        EditorDockDropOperation operation)
    {
        var sourceWindow = FindWindow(tab);
        if (sourceWindow is null)
        {
            return;
        }

        sourceWindow.Remove(tab);

        var insertedWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        insertedWindow.Add(tab);
        windowsById_.Add(insertedWindow.Id, insertedWindow);

        var insertedNode = new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
        InsertWindowNodeAtWorkspaceEdge(operation, insertedNode);

        RemoveWindowIfEmpty(sourceWindow);
        NormalizeLayoutGraph();
        SetActiveWindow(insertedWindow);
    }

    private bool CanAcceptDetachedTab(EditorDockDropTarget target)
    {
        if (!target.IsAccepted)
        {
            return false;
        }

        if (target.Operation == EditorDockDropOperation.TabInto
            && target.TargetId is { } targetWindowId)
        {
            return windowsById_.ContainsKey(targetWindowId);
        }

        if (target.Operation == EditorDockDropOperation.InsertTabAtIndex
            && target.TargetId is { } tabInsertTargetWindowId
            && target.TargetIndex is >= 0)
        {
            return windowsById_.ContainsKey(tabInsertTargetWindowId);
        }

        if (target.Operation == EditorDockDropOperation.SplitBetween
            && target.TargetId is { } targetSplitId)
        {
            return FindSplitNode(RootNode, targetSplitId) is not null;
        }

        if (IsWindowInsertOperation(target.Operation)
            && target.TargetId is { } insertTargetWindowId)
        {
            return TryFindWindowNode(
                RootNode,
                insertTargetWindowId,
                parent: null,
                out _,
                out _,
                out var targetNode)
                && targetNode is not null;
        }

        if (IsWorkspaceEdgeInsertOperation(target.Operation))
        {
            return RootNode is not null;
        }

        return false;
    }

    private void InsertDetachedTab(
        EditorDockTabViewModel tab,
        EditorDockDropTarget target,
        DockArea fallbackArea)
    {
        if (target.Operation == EditorDockDropOperation.TabInto
            && target.TargetId is { } targetWindowId
            && windowsById_.TryGetValue(targetWindowId, out var targetWindow))
        {
            targetWindow.Add(tab);
            targetWindow.Activate(tab);
            SetActiveWindow(targetWindow);
            return;
        }

        if (target.Operation == EditorDockDropOperation.InsertTabAtIndex
            && target.TargetId is { } tabInsertTargetWindowId
            && target.TargetIndex is { } tabInsertTargetIndex
            && windowsById_.TryGetValue(tabInsertTargetWindowId, out var tabInsertTargetWindow))
        {
            tabInsertTargetWindow.Insert(tab, tabInsertTargetIndex);
            tabInsertTargetWindow.Activate(tab);
            SetActiveWindow(tabInsertTargetWindow);
            return;
        }

        if (target.Operation == EditorDockDropOperation.SplitBetween
            && target.TargetId is { } targetSplitId)
        {
            var targetSplit = FindSplitNode(RootNode, targetSplitId);
            if (targetSplit is null)
            {
                return;
            }

            var insertedNode = CreateDetachedWindowNode(tab, fallbackArea);
            InsertWindowNodeAtSplitter(targetSplit, insertedNode);
            NormalizeLayoutGraph();
            return;
        }

        if (IsWorkspaceEdgeInsertOperation(target.Operation))
        {
            InsertDetachedTabAtWorkspaceEdge(tab, target.Operation, fallbackArea);
            NormalizeLayoutGraph();
            return;
        }

        if (IsWindowInsertOperation(target.Operation)
            && target.TargetId is { } insertTargetWindowId
            && TryFindWindowNode(
                RootNode,
                insertTargetWindowId,
                parent: null,
                out _,
                out _,
                out var targetNode)
            && targetNode is not null)
        {
            var insertedNode = CreateDetachedWindowNode(tab, fallbackArea);
            var replacement = CreateWindowInsertionSplit(target.Operation, targetNode, insertedNode);
            ReplaceNode(targetNode, replacement);
            NormalizeLayoutGraph();
        }
    }

    private void InsertDetachedTabAtWorkspaceEdge(
        EditorDockTabViewModel tab,
        EditorDockDropOperation operation,
        DockArea fallbackArea)
    {
        var insertedNode = CreateDetachedWindowNode(tab, fallbackArea);
        InsertWindowNodeAtWorkspaceEdge(operation, insertedNode);
    }

    private EditorDockWindowNodeViewModel CreateDetachedWindowNode(EditorDockTabViewModel tab, DockArea fallbackArea)
    {
        var insertedWindow = CreateDynamicWindow(tab, fallbackArea);
        insertedWindow.Add(tab);
        windowsById_.Add(insertedWindow.Id, insertedWindow);
        SetActiveWindow(insertedWindow);
        return new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
    }

    private EditorDockWindowViewModel CreateDynamicWindow(
        EditorDockTabViewModel tab,
        DockArea area)
    {
        var index = nextDynamicWindowIndex_++;
        return new EditorDockWindowViewModel(
            $"{DynamicWindowIdPrefix}{index}",
            tab.Title,
            area,
            "Dock window");
    }

    private static int GetNextDynamicWindowIndex(IEnumerable<EditorDockWindowViewModel> windows)
    {
        var nextIndex = 1;
        foreach (var window in windows)
        {
            if (!window.Id.StartsWith(DynamicWindowIdPrefix, StringComparison.Ordinal))
            {
                continue;
            }

            var suffix = window.Id[DynamicWindowIdPrefix.Length..];
            if (int.TryParse(suffix, out var index) && index >= nextIndex)
            {
                nextIndex = index + 1;
            }
        }

        return nextIndex;
    }

    private void InsertWindowNodeAtSplitter(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode)
    {
        if (ShouldInsertIntoFirstSide(targetSplit))
        {
            targetSplit.First = new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                targetSplit.Orientation,
                targetSplit.First,
                insertedNode,
                new GridLength(1, GridUnitType.Star),
                GetInsertedNodeLength(targetSplit.Orientation));
            return;
        }

        targetSplit.Second = new EditorDockSplitNodeViewModel(
            CreateDynamicSplitId(),
            targetSplit.Orientation,
            insertedNode,
            targetSplit.Second,
            GetInsertedNodeLength(targetSplit.Orientation),
            new GridLength(1, GridUnitType.Star));
    }

    private void InsertWindowNodeAtWorkspaceEdge(
        EditorDockDropOperation operation,
        EditorDockWindowNodeViewModel insertedNode)
    {
        if (RootNode is null)
        {
            RootNode = insertedNode;
            return;
        }

        var currentRoot = RootNode;
        RootNode = operation switch
        {
            EditorDockDropOperation.InsertWorkspaceLeft => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Horizontal,
                insertedNode,
                currentRoot,
                GetInsertedNodeLength(Orientation.Horizontal),
                new GridLength(1, GridUnitType.Star)),
            EditorDockDropOperation.InsertWorkspaceRight => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Horizontal,
                currentRoot,
                insertedNode,
                new GridLength(1, GridUnitType.Star),
                GetInsertedNodeLength(Orientation.Horizontal)),
            EditorDockDropOperation.InsertWorkspaceTop => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Vertical,
                insertedNode,
                currentRoot,
                GetInsertedNodeLength(Orientation.Vertical),
                new GridLength(1, GridUnitType.Star)),
            EditorDockDropOperation.InsertWorkspaceBottom => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Vertical,
                currentRoot,
                insertedNode,
                new GridLength(1, GridUnitType.Star),
                GetInsertedNodeLength(Orientation.Vertical)),
            _ => currentRoot,
        };
    }

    private string CreateDynamicSplitId()
    {
        return $"split-user-{nextDynamicSplitIndex_++}";
    }

    private static bool ShouldInsertIntoFirstSide(EditorDockSplitNodeViewModel split)
    {
        return split.FirstLength.IsStar && !split.SecondLength.IsStar;
    }

    private static GridLength GetInsertedNodeLength(Orientation orientation)
    {
        return orientation == Orientation.Horizontal
            ? new GridLength(240)
            : new GridLength(180);
    }

    private EditorDockSplitNodeViewModel CreateWindowInsertionSplit(
        EditorDockDropOperation operation,
        EditorDockWindowNodeViewModel targetNode,
        EditorDockWindowNodeViewModel insertedNode)
    {
        return operation switch
        {
            EditorDockDropOperation.InsertLeft => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Horizontal,
                insertedNode,
                targetNode,
                GetInsertedNodeLength(Orientation.Horizontal),
                new GridLength(1, GridUnitType.Star)),
            EditorDockDropOperation.InsertRight => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Horizontal,
                targetNode,
                insertedNode,
                new GridLength(1, GridUnitType.Star),
                GetInsertedNodeLength(Orientation.Horizontal)),
            EditorDockDropOperation.InsertTop => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Vertical,
                insertedNode,
                targetNode,
                GetInsertedNodeLength(Orientation.Vertical),
                new GridLength(1, GridUnitType.Star)),
            EditorDockDropOperation.InsertBottom => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Vertical,
                targetNode,
                insertedNode,
                new GridLength(1, GridUnitType.Star),
                GetInsertedNodeLength(Orientation.Vertical)),
            _ => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Horizontal,
                targetNode,
                insertedNode,
                new GridLength(1, GridUnitType.Star),
                GetInsertedNodeLength(Orientation.Horizontal)),
        };
    }

    private static bool IsWindowInsertOperation(EditorDockDropOperation operation)
    {
        return operation is EditorDockDropOperation.InsertLeft
            or EditorDockDropOperation.InsertRight
            or EditorDockDropOperation.InsertTop
            or EditorDockDropOperation.InsertBottom;
    }

    private static bool IsWorkspaceEdgeInsertOperation(EditorDockDropOperation operation)
    {
        return operation is EditorDockDropOperation.InsertWorkspaceLeft
            or EditorDockDropOperation.InsertWorkspaceRight
            or EditorDockDropOperation.InsertWorkspaceTop
            or EditorDockDropOperation.InsertWorkspaceBottom;
    }

    private EditorDockSplitNodeViewModel? FindSplitNode(EditorDockNodeViewModel? node, string splitId)
    {
        if (node is not EditorDockSplitNodeViewModel split)
        {
            return null;
        }

        if (split.Id == splitId)
        {
            return split;
        }

        return FindSplitNode(split.First, splitId)
            ?? FindSplitNode(split.Second, splitId);
    }

    private void RemoveWindowIfEmpty(EditorDockWindowViewModel window)
    {
        if (window.Tabs.Count > 0)
        {
            return;
        }

        var isActiveWindow = ReferenceEquals(activeWindow_, window);
        if (!TryFindWindowNode(
                RootNode,
                window.Id,
                parent: null,
                out var parentSplit,
                out var isFirstChild,
                out _))
        {
            return;
        }

        windowsById_.Remove(window.Id);
        if (parentSplit is null)
        {
            RootNode = null;
            if (isActiveWindow)
            {
                SetActiveWindow(null);
            }

            return;
        }

        var sibling = isFirstChild ? parentSplit.Second : parentSplit.First;
        ReplaceNode(parentSplit, sibling);
        if (isActiveWindow)
        {
            SetActiveWindow(FindFirstWindowWithContent());
        }
    }

    private bool TryFindWindowNode(
        EditorDockNodeViewModel? node,
        string windowId,
        EditorDockSplitNodeViewModel? parent,
        out EditorDockSplitNodeViewModel? parentSplit,
        out bool isFirstChild,
        out EditorDockWindowNodeViewModel? windowNode)
    {
        if (node is EditorDockWindowNodeViewModel window && window.Window.Id == windowId)
        {
            parentSplit = parent;
            isFirstChild = parent is not null && ReferenceEquals(parent.First, node);
            windowNode = window;
            return true;
        }

        if (node is EditorDockSplitNodeViewModel split)
        {
            if (TryFindWindowNode(split.First, windowId, split, out parentSplit, out isFirstChild, out windowNode))
            {
                return true;
            }

            if (TryFindWindowNode(split.Second, windowId, split, out parentSplit, out isFirstChild, out windowNode))
            {
                return true;
            }
        }

        parentSplit = null;
        isFirstChild = false;
        windowNode = null;
        return false;
    }

    private bool ReplaceNode(EditorDockNodeViewModel target, EditorDockNodeViewModel replacement)
    {
        if (RootNode is null)
        {
            return false;
        }

        if (ReferenceEquals(RootNode, target))
        {
            RootNode = replacement;
            return true;
        }

        return ReplaceNode(RootNode, target, replacement);
    }

    private bool ReplaceNode(
        EditorDockNodeViewModel? current,
        EditorDockNodeViewModel target,
        EditorDockNodeViewModel replacement)
    {
        if (current is not EditorDockSplitNodeViewModel split)
        {
            return false;
        }

        if (ReferenceEquals(split.First, target))
        {
            split.First = replacement;
            return true;
        }

        if (ReferenceEquals(split.Second, target))
        {
            split.Second = replacement;
            return true;
        }

        return ReplaceNode(split.First, target, replacement)
            || ReplaceNode(split.Second, target, replacement);
    }

    private void NormalizeLayoutGraph()
    {
        if (RootNode is not null)
        {
            RootNode = NormalizeNode(RootNode);
        }
    }

    private EditorDockNodeViewModel NormalizeNode(EditorDockNodeViewModel node)
    {
        if (node is not EditorDockSplitNodeViewModel split)
        {
            return node;
        }

        split.First = NormalizeNode(split.First);
        split.Second = NormalizeNode(split.Second);

        if (!IsUserSplit(split))
        {
            return split;
        }

        var children = new List<EditorDockNodeViewModel>();
        CollectUserSplitChildren(split, split.Orientation, children);

        if (children.Count == 0)
        {
            return split;
        }

        if (children.Count == 1)
        {
            return children[0];
        }

        if (children.Count == 2)
        {
            split.First = children[0];
            split.Second = children[1];
            split.FirstLength = new GridLength(1, GridUnitType.Star);
            split.SecondLength = new GridLength(1, GridUnitType.Star);
            return split;
        }

        return BuildBalancedUserSplit(split.Orientation, children, 0, children.Count);
    }

    private void CollectUserSplitChildren(
        EditorDockNodeViewModel node,
        Orientation orientation,
        List<EditorDockNodeViewModel> children)
    {
        if (node is EditorDockSplitNodeViewModel split
            && split.Orientation == orientation
            && IsUserSplit(split))
        {
            CollectUserSplitChildren(split.First, orientation, children);
            CollectUserSplitChildren(split.Second, orientation, children);
            return;
        }

        children.Add(node);
    }

    private EditorDockNodeViewModel BuildBalancedUserSplit(
        Orientation orientation,
        IReadOnlyList<EditorDockNodeViewModel> children,
        int start,
        int count)
    {
        if (count == 1)
        {
            return children[start];
        }

        var firstCount = count / 2;
        var secondCount = count - firstCount;
        return new EditorDockSplitNodeViewModel(
            CreateDynamicSplitId(),
            orientation,
            BuildBalancedUserSplit(orientation, children, start, firstCount),
            BuildBalancedUserSplit(orientation, children, start + firstCount, secondCount),
            new GridLength(1, GridUnitType.Star),
            new GridLength(1, GridUnitType.Star));
    }

    private static bool IsUserSplit(EditorDockSplitNodeViewModel split)
    {
        return split.Id.StartsWith("split-user-", StringComparison.Ordinal);
    }

    private static EditorDockTabViewModel CreateTab(PanelDescriptor descriptor)
    {
        return new EditorDockTabViewModel(
            descriptor.Id,
            descriptor.Title,
            GetTag(descriptor),
            GetTitleDetail(descriptor),
            GetStatusText(descriptor),
            descriptor.Kind,
            descriptor.DefaultArea,
            descriptor.CreateContent());
    }

    private EditorDockNodeViewModel CreateDefaultLayout()
    {
        var centerAndBottom = new EditorDockSplitNodeViewModel(
            "split-center-bottom",
            Orientation.Vertical,
            new EditorDockWindowNodeViewModel("node-center", CenterWindow),
            new EditorDockWindowNodeViewModel("node-bottom", BottomWindow),
            new GridLength(1, GridUnitType.Star),
            new GridLength(210));

        var workAndInspector = new EditorDockSplitNodeViewModel(
            "split-work-inspector",
            Orientation.Horizontal,
            centerAndBottom,
            new EditorDockWindowNodeViewModel("node-right", RightWindow),
            new GridLength(1, GridUnitType.Star),
            new GridLength(320));

        return new EditorDockSplitNodeViewModel(
            "split-left-work",
            Orientation.Horizontal,
            new EditorDockWindowNodeViewModel("node-left", LeftWindow),
            workAndInspector,
            new GridLength(260),
            new GridLength(1, GridUnitType.Star));
    }

    private static string GetTag(PanelDescriptor descriptor)
    {
        return descriptor.Kind == PanelKind.Document ? "DOC" : descriptor.DefaultArea.ToString().ToUpperInvariant();
    }

    private static string GetTitleDetail(PanelDescriptor descriptor)
    {
        return descriptor.Id switch
        {
            "scene-view" => "custom viewport shell",
            "hierarchy" => "selection source",
            "inspector" => "context target",
            "console" => "runtime log stream",
            "problems" => "validation queue",
            _ => descriptor.MenuPath,
        };
    }

    private static string GetStatusText(PanelDescriptor descriptor)
    {
        return descriptor.Id switch
        {
            "scene-view" => "live",
            "problems" => "0",
            "console" => "idle",
            _ => descriptor.Kind.ToString().ToLowerInvariant(),
        };
    }
}
