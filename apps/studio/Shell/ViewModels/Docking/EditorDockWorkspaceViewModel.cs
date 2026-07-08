using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Layout;
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Editor.Shell.Docking.DropTargets;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Docking.Panels;
using Editor.Shell.Services;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Docking;

public sealed class EditorDockWorkspaceViewModel : ViewModelBase, IDisposable
{
    private const string DynamicWindowIdPrefix = "owned-dock-window-";
    private const string DynamicSplitIdPrefix = "split-user-";
    private const string LayoutNodeKindSplit = "Split";
    private const string LayoutNodeKindWindow = "Window";
    private readonly IPanelRegistry? panelRegistry_;
    private readonly PanelInstanceManager panelInstanceManager_;
    private readonly Dictionary<DockArea, EditorDockWindowViewModel> windowsByArea_;
    private readonly Dictionary<string, EditorDockWindowViewModel> windowsById_;
    private EditorDockNodeViewModel? rootNode_;
    private EditorDockWindowViewModel? activeWindow_;
    private EditorDockTabViewModel? activeLifecycleTab_;
    private EditorDockWindowViewModel? dragSourceWindow_;
    private EditorDockTabViewModel? dragSourceTab_;
    private EditorDockWindowViewModel? tabInsertPreviewWindow_;
    private bool isHostFocused_ = true;
    private int nextDynamicWindowIndex_ = 1;
    private int nextDynamicSplitIndex_ = 1;

    public event EventHandler? DockContentChanged;

    internal EditorPanelFrameScheduler PanelFrameScheduler { get; }

    public EditorDockWorkspaceViewModel(
        IPanelRegistry panelRegistry,
        IEditorLifecycleEventService? lifecycleEvents = null,
        EditorPanelFrameScheduler? panelFrameScheduler = null)
    {
        panelRegistry_ = panelRegistry;
        LifecycleEvents = lifecycleEvents ?? new EditorLifecycleEventService();
        PanelFrameScheduler = panelFrameScheduler ?? new EditorPanelFrameScheduler();
        panelInstanceManager_ = new PanelInstanceManager(PanelFrameScheduler);
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
            window.Add(CreateTab(descriptor, window.Area));
        }

        SetActiveWindow(CenterWindow.Tabs.Count > 0 ? CenterWindow : FindFirstWindowWithContent());
    }

    private EditorDockWorkspaceViewModel(
        EditorDockWindowViewModel floatingDockWindow,
        IEditorLifecycleEventService lifecycleEvents,
        EditorPanelFrameScheduler panelFrameScheduler)
    {
        panelRegistry_ = null;
        LifecycleEvents = lifecycleEvents;
        PanelFrameScheduler = panelFrameScheduler;
        panelInstanceManager_ = new PanelInstanceManager(PanelFrameScheduler);
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
        SetPanelLifecycleHostKind(floatingDockWindow, isFloatingWorkspace: true);
        nextDynamicWindowIndex_ = GetNextDynamicWindowIndex(windowsById_.Values);
        rootNode_ = new EditorDockWindowNodeViewModel($"node-{floatingDockWindow.Id}", floatingDockWindow);
        SetActiveWindow(floatingDockWindow);
    }

    private EditorDockWorkspaceViewModel(
        IPanelRegistry panelRegistry,
        EditorDockFloatingWindowSnapshot snapshot,
        IEditorLifecycleEventService lifecycleEvents,
        EditorPanelFrameScheduler? panelFrameScheduler = null)
    {
        panelRegistry_ = panelRegistry;
        LifecycleEvents = lifecycleEvents;
        PanelFrameScheduler = panelFrameScheduler ?? new EditorPanelFrameScheduler();
        panelInstanceManager_ = new PanelInstanceManager(PanelFrameScheduler);
        WorkspaceKind = EditorDockWorkspaceKind.FloatingWindow;
        var fallbackWindow = new EditorDockWindowViewModel(
            "owned-dock-floating-restore",
            "Floating",
            DockArea.Center,
            "Floating workspace");
        LeftWindow = fallbackWindow;
        CenterWindow = fallbackWindow;
        BottomWindow = fallbackWindow;
        RightWindow = fallbackWindow;
        windowsByArea_ = [];
        windowsById_ = [];

        var descriptorsById = CreatePanelDescriptorsById();
        var usedTabIds = new HashSet<string>(StringComparer.Ordinal);
        rootNode_ = snapshot.Root is null
            ? null
            : RestoreLayoutNode(snapshot.Root, descriptorsById, usedTabIds);
        nextDynamicWindowIndex_ = GetNextDynamicWindowIndex(windowsById_.Values);
        nextDynamicSplitIndex_ = GetNextDynamicSplitIndex(rootNode_);
        SetActiveWindow(
            snapshot.ActiveWindowId is not null
                && windowsById_.TryGetValue(snapshot.ActiveWindowId, out var activeWindow)
                    ? activeWindow
                    : FindFirstWindowWithContent());
    }

    public static bool TryCreateFloatingWorkspace(
        IPanelRegistry panelRegistry,
        EditorDockFloatingWindowSnapshot snapshot,
        out EditorDockWorkspaceViewModel workspace)
    {
        return TryCreateFloatingWorkspace(
            panelRegistry,
            snapshot,
            new EditorLifecycleEventService(),
            out workspace);
    }

    public static bool TryCreateFloatingWorkspace(
        IPanelRegistry panelRegistry,
        EditorDockFloatingWindowSnapshot snapshot,
        IEditorLifecycleEventService lifecycleEvents,
        out EditorDockWorkspaceViewModel workspace)
    {
        workspace = new EditorDockWorkspaceViewModel(panelRegistry, snapshot, lifecycleEvents);
        if (workspace.RootNode is not null && workspace.HasDockContent())
        {
            return true;
        }

        workspace = null!;
        return false;
    }

    public EditorDockWindowViewModel LeftWindow { get; }

    public EditorDockWindowViewModel CenterWindow { get; }

    public EditorDockWindowViewModel BottomWindow { get; }

    public EditorDockWindowViewModel RightWindow { get; }

    public EditorDockWorkspaceKind WorkspaceKind { get; }

    public IEditorLifecycleEventService LifecycleEvents { get; }

    public bool IsMainWindow => WorkspaceKind == EditorDockWorkspaceKind.MainWindow;

    public bool IsFloatingWindow => WorkspaceKind == EditorDockWorkspaceKind.FloatingWindow;

    public string WorkspaceKindText => IsFloatingWindow ? "Floating Window" : "Main Window";

    public bool IsHostFocused => isHostFocused_;

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

    public EditorDockLayoutSnapshot CaptureLayoutSnapshot()
    {
        return new EditorDockLayoutSnapshot
        {
            Version = 1,
            ActiveWindowId = ActiveWindow?.Id,
            Root = CaptureLayoutNode(RootNode),
        };
    }

    public bool RestoreLayoutSnapshot(EditorDockLayoutSnapshot? snapshot)
    {
        if (panelRegistry_ is null
            || snapshot?.Root is null
            || snapshot.Version != 1)
        {
            return false;
        }

        ClearTransientDockState();
        ResetWorkspaceWindows();

        var descriptorsById = CreatePanelDescriptorsById();
        var usedTabIds = new HashSet<string>(StringComparer.Ordinal);
        var restoredRoot = RestoreLayoutNode(snapshot.Root, descriptorsById, usedTabIds);
        if (restoredRoot is null)
        {
            ResetLayout();
            return false;
        }

        RootNode = restoredRoot;
        nextDynamicWindowIndex_ = GetNextDynamicWindowIndex(windowsById_.Values);
        nextDynamicSplitIndex_ = GetNextDynamicSplitIndex(RootNode);
        SetActiveWindow(
            snapshot.ActiveWindowId is not null
                && windowsById_.TryGetValue(snapshot.ActiveWindowId, out var activeWindow)
                    ? activeWindow
                    : FindFirstWindowWithContent());
        NotifyDockContentChanged();
        return true;
    }

    public void ResetLayout()
    {
        if (panelRegistry_ is null)
        {
            return;
        }

        ClearTransientDockState();
        ResetWorkspaceWindows();
        nextDynamicWindowIndex_ = 1;
        nextDynamicSplitIndex_ = 1;
        RootNode = CreateDefaultLayout();

        foreach (var descriptor in panelRegistry_.GetAll())
        {
            var window = windowsByArea_[descriptor.DefaultArea];
            window.Add(CreateTab(descriptor, window.Area));
        }

        SetActiveWindow(CenterWindow.Tabs.Count > 0 ? CenterWindow : FindFirstWindowWithContent());
        NotifyDockContentChanged();
    }

    public bool ActivatePanel(string panelId)
    {
        if (!TryFindPanelTab(panelId, out var window, out var tab))
        {
            return false;
        }

        window.Activate(tab);
        SetActiveWindow(window);
        return true;
    }

    public bool OpenPanel(string panelId)
    {
        if (!IsMainWindow
            || panelRegistry_ is null
            || string.IsNullOrWhiteSpace(panelId))
        {
            return false;
        }

        if (ActivatePanel(panelId))
        {
            return true;
        }

        var descriptor = panelRegistry_.GetRequired(panelId);
        var targetWindow = GetPanelOpenTargetWindow(descriptor.DefaultArea);
        if (targetWindow is null)
        {
            return false;
        }

        var tab = CreateTab(descriptor, targetWindow.Area);
        targetWindow.Add(tab);
        targetWindow.Activate(tab);
        SetActiveWindow(targetWindow);
        NotifyDockContentChanged();
        return true;
    }

    public bool CanOpenPanel(string? panelId)
    {
        if (!IsMainWindow
            || panelRegistry_ is null
            || string.IsNullOrWhiteSpace(panelId))
        {
            return false;
        }

        if (ContainsPanel(panelId))
        {
            return true;
        }

        foreach (var descriptor in panelRegistry_.GetAll())
        {
            if (string.Equals(descriptor.Id, panelId, StringComparison.Ordinal)
                && windowsByArea_.ContainsKey(descriptor.DefaultArea))
            {
                return true;
            }
        }

        return false;
    }

    public void BeginDrag(EditorDockTabViewModel tab)
    {
        var window = FindWindow(tab);
        if (window is null)
        {
            return;
        }

        window.Activate(tab);
        SetActiveWindow(window);
        SetDragSourceState(window, tab);
        DragState.Begin(tab);
    }

    public bool ActivateTab(EditorDockTabViewModel tab)
    {
        var window = FindWindow(tab);
        if (window is null)
        {
            return false;
        }

        window.Activate(tab);
        SetActiveWindow(window);
        return true;
    }

    public bool ReorderTabInWindow(
        EditorDockWindowViewModel window,
        EditorDockTabViewModel tab,
        int targetIndex)
    {
        if (targetIndex < 0 || !ReferenceEquals(FindWindow(tab), window))
        {
            return false;
        }

        var moved = window.Move(tab, targetIndex);
        window.Activate(tab);
        SetActiveWindow(window);
        return moved;
    }

    public void BeginExternalDragPreview(EditorDockTabViewModel tab)
    {
        if (!DragState.IsActive || !ReferenceEquals(DragState.DraggedTab, tab))
        {
            DragState.Begin(tab);
        }
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

        return targetWindow.ShowTabInsertPlaceholder(tab, targetIndex, showsTab: false) || changed;
    }

    public bool WouldPreviewTabInsertChange(EditorDockDropTarget target)
    {
        var tab = DragState.DraggedTab;
        if (tab is null
            || target.Operation != EditorDockDropOperation.InsertTabAtIndex
            || target.TargetId is not { } targetWindowId
            || target.TargetIndex is not { } targetIndex)
        {
            return tabInsertPreviewWindow_ is not null;
        }

        if (!windowsById_.TryGetValue(targetWindowId, out var targetWindow))
        {
            return tabInsertPreviewWindow_ is not null;
        }

        return !ReferenceEquals(tabInsertPreviewWindow_, targetWindow)
            || !targetWindow.IsTabInsertPlaceholderCurrent(tab, targetIndex, showsTab: false);
    }

    internal bool TryGetTabInsertPreview(out string windowId, out int targetIndex)
    {
        if (tabInsertPreviewWindow_?.TabInsertPlaceholderIndex is { } currentTargetIndex)
        {
            windowId = tabInsertPreviewWindow_.Id;
            targetIndex = currentTargetIndex;
            return true;
        }

        windowId = string.Empty;
        targetIndex = -1;
        return false;
    }

    internal void SetHostFocusState(bool isHostFocused)
    {
        if (!SetProperty(ref isHostFocused_, isHostFocused, nameof(IsHostFocused)))
        {
            return;
        }

        foreach (var window in windowsById_.Values)
        {
            window.SetHostFocusState(isHostFocused);
        }
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
                && target.TargetId is not null)
            {
                InsertTabAtSplitter(tab, target);
            }
            else if (IsWindowInsertOperation(target.Operation)
                && target.TargetId is { } insertTargetWindowId)
            {
                InsertTabAdjacentToWindow(tab, insertTargetWindowId, target.Operation);
            }
            else if (IsWorkspaceEdgeInsertOperation(target.Operation))
            {
                InsertTabAtWorkspaceEdge(tab, target);
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
            NotifyDockContentChanged();
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
            if (sourceWindow is null)
            {
                return null;
            }

            if (target.Operation == EditorDockDropOperation.Float)
            {
                return FloatTab(tab, target.PreviewBounds);
            }

            if (!targetWorkspace.CanAcceptDetachedTab(target))
            {
                return null;
            }

            var sourceArea = sourceWindow.Area;
            sourceWindow.Remove(tab);
            SetActiveWindow(sourceWindow.Tabs.Count > 0 ? sourceWindow : FindFirstWindowWithContent());
            targetWorkspace.InsertDetachedTab(tab, target, sourceArea);
            RemoveWindowIfEmpty(sourceWindow);
            NormalizeLayoutGraph();
            return null;
        }
        finally
        {
            ClearTabInsertPreview();
            ClearDragSourceState();
            DragState.Clear();
            targetWorkspace.ClearExternalDragPreview();
            NotifyDockContentChanged();
            targetWorkspace.NotifyDockContentChanged();
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

    public bool CloseTab(EditorDockTabViewModel tab)
    {
        var sourceWindow = FindWindow(tab);
        if (sourceWindow is null)
        {
            return false;
        }

        sourceWindow.Remove(tab);
        tab.ReleasePanelInstance();
        RemoveWindowIfEmpty(sourceWindow);
        NormalizeLayoutGraph();
        SetActiveWindow(sourceWindow.Tabs.Count > 0 ? sourceWindow : FindFirstWindowWithContent());
        NotifyDockContentChanged();
        return true;
    }

    public bool ClosePanel(string panelId)
    {
        return TryFindPanelTab(panelId, out _, out var tab)
            && CloseTab(tab);
    }

    public void Dispose()
    {
        ResetWorkspaceWindows();
        panelInstanceManager_.Dispose();
    }

    public bool ContainsPanel(string panelId)
    {
        return TryFindPanelTab(panelId, out _, out _);
    }

    private bool TryFindPanelTab(
        string panelId,
        out EditorDockWindowViewModel window,
        out EditorDockTabViewModel tab)
    {
        if (string.IsNullOrWhiteSpace(panelId))
        {
            window = null!;
            tab = null!;
            return false;
        }

        foreach (var candidateWindow in windowsById_.Values)
        {
            foreach (var candidateTab in candidateWindow.Tabs)
            {
                if (!string.Equals(candidateTab.Id, panelId, StringComparison.Ordinal))
                {
                    continue;
                }

                window = candidateWindow;
                tab = candidateTab;
                return true;
            }
        }

        window = null!;
        tab = null!;
        return false;
    }

    private void NotifyDockContentChanged()
    {
        DockContentChanged?.Invoke(this, EventArgs.Empty);
    }

    private EditorDockLayoutNodeSnapshot? CaptureLayoutNode(EditorDockNodeViewModel? node)
    {
        return node switch
        {
            EditorDockWindowNodeViewModel windowNode => new EditorDockLayoutNodeSnapshot
            {
                Kind = LayoutNodeKindWindow,
                Id = windowNode.Id,
                WindowId = windowNode.Window.Id,
                WindowTitle = windowNode.Window.Title,
                WindowArea = windowNode.Window.Area,
                WindowRole = windowNode.Window.Role,
                ActiveTabId = windowNode.Window.ActiveTab?.Id,
                TabIds = CaptureTabIds(windowNode.Window),
            },
            EditorDockSplitNodeViewModel splitNode => new EditorDockLayoutNodeSnapshot
            {
                Kind = LayoutNodeKindSplit,
                Id = splitNode.Id,
                Orientation = splitNode.Orientation,
                FirstLength = CaptureGridLength(splitNode.FirstLength),
                SecondLength = CaptureGridLength(splitNode.SecondLength),
                First = CaptureLayoutNode(splitNode.First),
                Second = CaptureLayoutNode(splitNode.Second),
            },
            _ => null,
        };
    }

    private static List<string> CaptureTabIds(EditorDockWindowViewModel window)
    {
        var ids = new List<string>(window.Tabs.Count);
        foreach (var tab in window.Tabs)
        {
            ids.Add(tab.Id);
        }

        return ids;
    }

    private static EditorDockGridLengthSnapshot CaptureGridLength(GridLength length)
    {
        return new EditorDockGridLengthSnapshot
        {
            Value = length.Value,
            Unit = length.GridUnitType,
        };
    }

    private EditorDockNodeViewModel? RestoreLayoutNode(
        EditorDockLayoutNodeSnapshot snapshot,
        IReadOnlyDictionary<string, PanelDescriptor> descriptorsById,
        HashSet<string> usedTabIds)
    {
        if (snapshot.Kind == LayoutNodeKindSplit)
        {
            var first = snapshot.First is null
                ? null
                : RestoreLayoutNode(snapshot.First, descriptorsById, usedTabIds);
            var second = snapshot.Second is null
                ? null
                : RestoreLayoutNode(snapshot.Second, descriptorsById, usedTabIds);
            if (first is null)
            {
                return second;
            }

            if (second is null)
            {
                return first;
            }

            return new EditorDockSplitNodeViewModel(
                string.IsNullOrWhiteSpace(snapshot.Id) ? CreateDynamicSplitId() : snapshot.Id,
                snapshot.Orientation,
                first,
                second,
                RestoreGridLength(snapshot.FirstLength),
                RestoreGridLength(snapshot.SecondLength));
        }

        if (snapshot.Kind != LayoutNodeKindWindow)
        {
            return null;
        }

        var window = RestoreWindow(snapshot, descriptorsById, usedTabIds);
        return window is null
            ? null
            : new EditorDockWindowNodeViewModel(
                string.IsNullOrWhiteSpace(snapshot.Id) ? $"node-{window.Id}" : snapshot.Id,
                window);
    }

    private EditorDockWindowViewModel? RestoreWindow(
        EditorDockLayoutNodeSnapshot snapshot,
        IReadOnlyDictionary<string, PanelDescriptor> descriptorsById,
        HashSet<string> usedTabIds)
    {
        var tabs = new List<EditorDockTabViewModel>();
        foreach (var tabId in snapshot.TabIds)
        {
            if (descriptorsById.TryGetValue(tabId, out var descriptor)
                && usedTabIds.Add(tabId))
            {
                tabs.Add(CreateTab(descriptor, snapshot.WindowArea));
            }
        }

        if (tabs.Count == 0)
        {
            return null;
        }

        var window = GetOrCreateRestoredWindow(snapshot, tabs[0]);
        foreach (var tab in tabs)
        {
            window.Add(tab);
        }

        if (snapshot.ActiveTabId is not null)
        {
            foreach (var tab in window.Tabs)
            {
                if (tab.Id == snapshot.ActiveTabId)
                {
                    window.Activate(tab);
                    return window;
                }
            }
        }

        window.Activate(window.Tabs[0]);
        return window;
    }

    private EditorDockWindowViewModel GetOrCreateRestoredWindow(
        EditorDockLayoutNodeSnapshot snapshot,
        EditorDockTabViewModel firstTab)
    {
        var windowId = string.IsNullOrWhiteSpace(snapshot.WindowId)
            ? $"{DynamicWindowIdPrefix}{nextDynamicWindowIndex_++}"
            : snapshot.WindowId;
        if (windowsById_.TryGetValue(windowId, out var existingWindow))
        {
            return existingWindow;
        }

        var window = new EditorDockWindowViewModel(
            windowId,
            string.IsNullOrWhiteSpace(snapshot.WindowTitle) ? firstTab.Title : snapshot.WindowTitle,
            snapshot.WindowArea,
            string.IsNullOrWhiteSpace(snapshot.WindowRole) ? "Restored panel" : snapshot.WindowRole);
        window.SetHostFocusState(IsHostFocused);
        windowsById_.Add(window.Id, window);
        return window;
    }

    private static GridLength RestoreGridLength(EditorDockGridLengthSnapshot? snapshot)
    {
        if (snapshot is null
            || double.IsNaN(snapshot.Value)
            || double.IsInfinity(snapshot.Value)
            || snapshot.Value <= 0)
        {
            return new GridLength(1, GridUnitType.Star);
        }

        return new GridLength(snapshot.Value, snapshot.Unit);
    }

    private Dictionary<string, PanelDescriptor> CreatePanelDescriptorsById()
    {
        var descriptors = new Dictionary<string, PanelDescriptor>(StringComparer.Ordinal);
        if (panelRegistry_ is null)
        {
            return descriptors;
        }

        foreach (var descriptor in panelRegistry_.GetAll())
        {
            descriptors[descriptor.Id] = descriptor;
        }

        return descriptors;
    }

    private void ResetWorkspaceWindows()
    {
        SetActivePanelLifecycle(null);
        var existingWindows = new List<EditorDockWindowViewModel>(windowsById_.Values);
        foreach (var window in existingWindows)
        {
            ReleaseWindowTabs(window);
            window.ResetTabs();
            window.SetActiveWindowState(false);
            window.SetDragSourceWindowState(false);
        }

        windowsById_.Clear();
        windowsByArea_.Clear();

        RegisterPrimaryWindow(LeftWindow);
        RegisterPrimaryWindow(CenterWindow);
        RegisterPrimaryWindow(BottomWindow);
        RegisterPrimaryWindow(RightWindow);
    }

    private void RegisterPrimaryWindow(EditorDockWindowViewModel window)
    {
        windowsById_[window.Id] = window;
        windowsByArea_[window.Area] = window;
    }

    private void ClearTransientDockState()
    {
        ClearTabInsertPreview();
        ClearDragSourceState();
        DragState.Clear();
        SetActivePanelLifecycle(null);
        activeWindow_?.SetActiveWindowState(false);
        activeWindow_ = null;
    }

    private static void ReleaseWindowTabs(EditorDockWindowViewModel window)
    {
        var tabs = new List<EditorDockTabViewModel>(window.Tabs);
        foreach (var tab in tabs)
        {
            tab.ReleasePanelInstance();
        }
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
            if (window.ContainsTab(tab))
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

    private EditorDockWindowViewModel? GetPanelOpenTargetWindow(DockArea defaultArea)
    {
        if (!windowsByArea_.TryGetValue(defaultArea, out var defaultWindow))
        {
            return null;
        }

        if (IsWindowInLayout(defaultWindow))
        {
            windowsById_[defaultWindow.Id] = defaultWindow;
            return defaultWindow;
        }

        RestorePrimaryWindow(defaultWindow);
        return defaultWindow;
    }

    private void RestorePrimaryWindow(EditorDockWindowViewModel window)
    {
        if (IsWindowInLayout(window))
        {
            windowsById_[window.Id] = window;
            return;
        }

        windowsById_[window.Id] = window;
        var insertedNode = new EditorDockWindowNodeViewModel(
            GetPrimaryWindowNodeId(window.Area),
            window);

        if (window.Area == DockArea.Center && TryRestoreCenterWindow(insertedNode))
        {
            return;
        }

        InsertWindowNodeAtWorkspaceEdge(GetWorkspaceEdgeOperation(window.Area), insertedNode);
    }

    private bool TryRestoreCenterWindow(EditorDockWindowNodeViewModel insertedNode)
    {
        return TryInsertPrimaryWindowAdjacentTo(DockArea.Bottom, EditorDockDropOperation.InsertTop, insertedNode)
            || TryInsertPrimaryWindowAdjacentTo(DockArea.Right, EditorDockDropOperation.InsertLeft, insertedNode)
            || TryInsertPrimaryWindowAdjacentTo(DockArea.Left, EditorDockDropOperation.InsertRight, insertedNode);
    }

    private bool TryInsertPrimaryWindowAdjacentTo(
        DockArea targetArea,
        EditorDockDropOperation operation,
        EditorDockWindowNodeViewModel insertedNode)
    {
        if (!windowsByArea_.TryGetValue(targetArea, out var targetWindow)
            || !TryFindWindowNode(
                RootNode,
                targetWindow.Id,
                parent: null,
                out _,
                out _,
                out var targetNode)
            || targetNode is null)
        {
            return false;
        }

        var replacement = CreateWindowInsertionSplit(operation, targetNode, insertedNode);
        return ReplaceNode(targetNode, replacement);
    }

    private bool IsWindowInLayout(EditorDockWindowViewModel window)
    {
        return TryFindWindowNode(
            RootNode,
            window.Id,
            parent: null,
            out _,
            out _,
            out _);
    }

    private static string GetPrimaryWindowNodeId(DockArea area)
    {
        return area switch
        {
            DockArea.Left => "node-left",
            DockArea.Center => "node-center",
            DockArea.Bottom => "node-bottom",
            DockArea.Right => "node-right",
            _ => $"node-{area.ToString().ToLowerInvariant()}",
        };
    }

    private static EditorDockDropOperation GetWorkspaceEdgeOperation(DockArea area)
    {
        return area switch
        {
            DockArea.Left => EditorDockDropOperation.InsertWorkspaceLeft,
            DockArea.Right => EditorDockDropOperation.InsertWorkspaceRight,
            DockArea.Bottom => EditorDockDropOperation.InsertWorkspaceBottom,
            _ => EditorDockDropOperation.InsertWorkspaceTop,
        };
    }

    private void SetActiveWindow(EditorDockWindowViewModel? window)
    {
        if (ReferenceEquals(activeWindow_, window))
        {
            UpdateActivePanelLifecycle();
            return;
        }

        activeWindow_?.SetActiveWindowState(false);
        activeWindow_ = window;
        activeWindow_?.SetActiveWindowState(true);
        UpdateActivePanelLifecycle();
        OnPropertyChanged(nameof(ActiveWindow));
        OnPropertyChanged(nameof(ActiveWindowTitle));
        OnPropertyChanged(nameof(HostTitle));
    }

    private void UpdateActivePanelLifecycle()
    {
        SetActivePanelLifecycle(activeWindow_?.ActiveTab);
    }

    private void SetActivePanelLifecycle(EditorDockTabViewModel? tab)
    {
        if (ReferenceEquals(activeLifecycleTab_, tab))
        {
            return;
        }

        activeLifecycleTab_?.DeactivatePanelInstance();
        activeLifecycleTab_ = tab;
        activeLifecycleTab_?.ActivatePanelInstance();
    }

    private static void SetPanelLifecycleHostKind(
        EditorDockWindowViewModel window,
        bool isFloatingWorkspace)
    {
        foreach (var tab in window.Tabs)
        {
            tab.SetPanelLifecycleHostKind(isFloatingWorkspace);
        }
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
        SetActiveWindow(sourceWindow.Tabs.Count > 0 ? sourceWindow : FindFirstWindowWithContent());

        var floatingDockWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        tab.SetPanelLifecycleHostKind(isFloatingWorkspace: true);
        floatingDockWindow.Add(tab);
        RemoveWindowIfEmpty(sourceWindow);
        NormalizeLayoutGraph();

        var floatingWorkspace = new EditorDockWorkspaceViewModel(
            floatingDockWindow,
            LifecycleEvents,
            PanelFrameScheduler);
        var floatingWindow = new EditorDockFloatingWindowViewModel(floatingWorkspace, LifecycleEvents);
        return new EditorDockFloatingWindowRequest(floatingWindow, bounds);
    }

    private void InsertTabAtSplitter(EditorDockTabViewModel tab, EditorDockDropTarget target)
    {
        var sourceWindow = FindWindow(tab);
        if (target.TargetId is not { } splitId)
        {
            return;
        }

        var targetSplit = FindSplitNode(RootNode, splitId);
        if (sourceWindow is null || targetSplit is null)
        {
            return;
        }

        if (IsSplitterInsertNoOp(targetSplit, sourceWindow))
        {
            sourceWindow.Activate(tab);
            SetActiveWindow(sourceWindow);
            return;
        }

        sourceWindow.Remove(tab);

        var insertedWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        insertedWindow.Add(tab);
        windowsById_.Add(insertedWindow.Id, insertedWindow);

        var insertedNode = new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
        InsertWindowNodeAtSplitter(targetSplit, insertedNode, target);

        RemoveWindowIfEmpty(sourceWindow);
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
        EditorDockDropTarget target)
    {
        var operation = target.Operation;
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
        tab.SetPanelFrameScheduler(PanelFrameScheduler);
        tab.SetPanelLifecycleHostKind(IsFloatingWindow);
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
            InsertWindowNodeAtSplitter(targetSplit, insertedNode, target);
            return;
        }

        if (IsWorkspaceEdgeInsertOperation(target.Operation))
        {
            InsertDetachedTabAtWorkspaceEdge(tab, target, fallbackArea);
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
        EditorDockDropTarget target,
        DockArea fallbackArea)
    {
        var insertedNode = CreateDetachedWindowNode(tab, fallbackArea);
        InsertWindowNodeAtWorkspaceEdge(target.Operation, insertedNode);
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
        var window = new EditorDockWindowViewModel(
            $"{DynamicWindowIdPrefix}{index}",
            tab.Title,
            area,
            "Dock window");
        window.SetHostFocusState(IsHostFocused);
        return window;
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

    private static int GetNextDynamicSplitIndex(EditorDockNodeViewModel? node)
    {
        var nextIndex = 1;
        CollectNextDynamicSplitIndex(node, ref nextIndex);
        return nextIndex;
    }

    private static void CollectNextDynamicSplitIndex(EditorDockNodeViewModel? node, ref int nextIndex)
    {
        if (node is not EditorDockSplitNodeViewModel split)
        {
            return;
        }

        if (split.Id.StartsWith(DynamicSplitIdPrefix, StringComparison.Ordinal))
        {
            var suffix = split.Id[DynamicSplitIdPrefix.Length..];
            if (int.TryParse(suffix, out var index) && index >= nextIndex)
            {
                nextIndex = index + 1;
            }
        }

        CollectNextDynamicSplitIndex(split.First, ref nextIndex);
        CollectNextDynamicSplitIndex(split.Second, ref nextIndex);
    }

    private void InsertWindowNodeAtSplitter(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        EditorDockDropTarget target)
    {
        if (!HasWeightedSplitLengths(targetSplit))
        {
            InsertWindowNodeAtSplitterLocally(targetSplit, insertedNode, target);
            return;
        }

        var entries = CreateSplitterInsertEntries(targetSplit, out var insertIndex);
        if (!TryInsertWeightedNode(entries, insertIndex, insertedNode))
        {
            return;
        }

        var rebuilt = BuildWeightedSplit(targetSplit.Orientation, entries, 0, entries.Count, out _);
        if (rebuilt is not EditorDockSplitNodeViewModel rebuiltSplit)
        {
            return;
        }

        targetSplit.First = rebuiltSplit.First;
        targetSplit.Second = rebuiltSplit.Second;
        targetSplit.FirstLength = rebuiltSplit.FirstLength;
        targetSplit.SecondLength = rebuiltSplit.SecondLength;
    }

    private void InsertWindowNodeAtSplitterLocally(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        EditorDockDropTarget target)
    {
        var replacement = CreateLocalSplitterInsertionSplit(targetSplit, insertedNode, target);
        ReplaceNode(targetSplit, replacement);
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
                GetInsertedWorkspaceSideEdgeLength(),
                GetRetainedWorkspaceSideEdgeLength()),
            EditorDockDropOperation.InsertWorkspaceRight => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Horizontal,
                currentRoot,
                insertedNode,
                GetRetainedWorkspaceSideEdgeLength(),
                GetInsertedWorkspaceSideEdgeLength()),
            EditorDockDropOperation.InsertWorkspaceTop => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Vertical,
                insertedNode,
                currentRoot,
                GetInsertedEdgeLength(),
                GetRetainedEdgeLength()),
            EditorDockDropOperation.InsertWorkspaceBottom => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Vertical,
                currentRoot,
                insertedNode,
                GetRetainedEdgeLength(),
                GetInsertedEdgeLength()),
            _ => currentRoot,
        };
    }

    private string CreateDynamicSplitId()
    {
        return $"{DynamicSplitIdPrefix}{nextDynamicSplitIndex_++}";
    }

    private static GridLength GetInsertedEdgeLength()
    {
        return new GridLength(1, GridUnitType.Star);
    }

    private static GridLength GetInsertedWorkspaceSideEdgeLength()
    {
        return new GridLength(1, GridUnitType.Star);
    }

    private static GridLength GetRetainedWorkspaceSideEdgeLength()
    {
        return new GridLength(4, GridUnitType.Star);
    }

    private static GridLength GetInsertedWindowSplitLength()
    {
        return new GridLength(1, GridUnitType.Star);
    }

    private static GridLength GetRetainedWindowSplitLength()
    {
        return new GridLength(1, GridUnitType.Star);
    }

    private static GridLength GetRetainedEdgeLength()
    {
        return new GridLength(2, GridUnitType.Star);
    }

    private static double GetSplitWeight(GridLength length)
    {
        if (!length.IsStar || double.IsNaN(length.Value) || double.IsInfinity(length.Value) || length.Value <= 0)
        {
            return 1d;
        }

        return Math.Clamp(length.Value, 0.05d, 16d);
    }

    private static bool HasWeightedSplitLengths(EditorDockSplitNodeViewModel split)
    {
        return HasWeightedSplitLength(split.FirstLength)
            && HasWeightedSplitLength(split.SecondLength);
    }

    private static bool HasWeightedSplitLength(GridLength length)
    {
        return length.IsStar
            && !double.IsNaN(length.Value)
            && !double.IsInfinity(length.Value)
            && length.Value > 0;
    }

    private EditorDockSplitNodeViewModel CreateLocalSplitterInsertionSplit(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        EditorDockDropTarget target)
    {
        if (TryCreateSymmetricLocalSplitterInsertion(targetSplit, insertedNode, out var symmetricSplit))
        {
            return symmetricSplit;
        }

        if (TryCreateMeasuredLocalSplitterInsertion(targetSplit, insertedNode, target, out var measuredSplit))
        {
            return measuredSplit;
        }

        return HasWeightedSplitLength(targetSplit.SecondLength) || !HasWeightedSplitLength(targetSplit.FirstLength)
            ? CreateTrailingLocalSplitterInsertion(targetSplit, insertedNode)
            : CreateLeadingLocalSplitterInsertion(targetSplit, insertedNode);
    }

    private bool TryCreateMeasuredLocalSplitterInsertion(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        EditorDockDropTarget target,
        out EditorDockSplitNodeViewModel replacement)
    {
        replacement = null!;
        if (target.SplitterFirstExtent is not { } firstExtent
            || target.SplitterSecondExtent is not { } secondExtent
            || firstExtent <= 0
            || secondExtent <= 0)
        {
            return false;
        }

        var retainedFirstLength = new GridLength(firstExtent / 2d, GridUnitType.Star);
        var insertedLength = new GridLength((firstExtent + secondExtent) / 2d, GridUnitType.Star);
        var retainedSecondLength = new GridLength(secondExtent / 2d, GridUnitType.Star);
        var trailingGroupLength = AddSplitLengths(insertedLength, retainedSecondLength);
        var trailingGroup = new EditorDockSplitNodeViewModel(
            CreateDynamicSplitId(),
            targetSplit.Orientation,
            insertedNode,
            targetSplit.Second,
            insertedLength,
            retainedSecondLength);

        replacement = new EditorDockSplitNodeViewModel(
            targetSplit.Id,
            targetSplit.Orientation,
            targetSplit.First,
            trailingGroup,
            retainedFirstLength,
            trailingGroupLength);
        return true;
    }

    private bool TryCreateSymmetricLocalSplitterInsertion(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        out EditorDockSplitNodeViewModel replacement)
    {
        replacement = null!;
        if (!CanScaleSplitLength(targetSplit.FirstLength)
            || !CanScaleSplitLength(targetSplit.SecondLength)
            || targetSplit.FirstLength.GridUnitType != targetSplit.SecondLength.GridUnitType)
        {
            return false;
        }

        var retainedFirstLength = ScaleSplitLength(targetSplit.FirstLength, 0.5d);
        var insertedFromFirstLength = ScaleSplitLength(targetSplit.FirstLength, 0.5d);
        var insertedFromSecondLength = ScaleSplitLength(targetSplit.SecondLength, 0.5d);
        var retainedSecondLength = ScaleSplitLength(targetSplit.SecondLength, 0.5d);
        var insertedLength = AddSplitLengths(insertedFromFirstLength, insertedFromSecondLength);
        var trailingGroupLength = AddSplitLengths(insertedLength, retainedSecondLength);
        var trailingGroup = new EditorDockSplitNodeViewModel(
            CreateDynamicSplitId(),
            targetSplit.Orientation,
            insertedNode,
            targetSplit.Second,
            insertedLength,
            retainedSecondLength);

        replacement = new EditorDockSplitNodeViewModel(
            targetSplit.Id,
            targetSplit.Orientation,
            targetSplit.First,
            trailingGroup,
            retainedFirstLength,
            trailingGroupLength);
        return true;
    }

    private EditorDockSplitNodeViewModel CreateTrailingLocalSplitterInsertion(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode)
    {
        var trailingGroup = new EditorDockSplitNodeViewModel(
            CreateDynamicSplitId(),
            targetSplit.Orientation,
            insertedNode,
            targetSplit.Second,
            new GridLength(1, GridUnitType.Star),
            new GridLength(1, GridUnitType.Star));

        return new EditorDockSplitNodeViewModel(
            targetSplit.Id,
            targetSplit.Orientation,
            targetSplit.First,
            trailingGroup,
            targetSplit.FirstLength,
            targetSplit.SecondLength);
    }

    private EditorDockSplitNodeViewModel CreateLeadingLocalSplitterInsertion(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode)
    {
        var leadingGroup = new EditorDockSplitNodeViewModel(
            CreateDynamicSplitId(),
            targetSplit.Orientation,
            targetSplit.First,
            insertedNode,
            new GridLength(1, GridUnitType.Star),
            new GridLength(1, GridUnitType.Star));

        return new EditorDockSplitNodeViewModel(
            targetSplit.Id,
            targetSplit.Orientation,
            leadingGroup,
            targetSplit.Second,
            targetSplit.FirstLength,
            targetSplit.SecondLength);
    }

    private static bool CanScaleSplitLength(GridLength length)
    {
        return !double.IsNaN(length.Value)
            && !double.IsInfinity(length.Value)
            && length.Value > 0
            && length.GridUnitType is GridUnitType.Star or GridUnitType.Pixel;
    }

    private static GridLength ScaleSplitLength(GridLength length, double factor)
    {
        var scaledValue = length.Value * factor;
        var minValue = length.GridUnitType == GridUnitType.Star ? 0.05d : 1d;
        return new GridLength(Math.Max(minValue, scaledValue), length.GridUnitType);
    }

    private static GridLength AddSplitLengths(GridLength first, GridLength second)
    {
        return new GridLength(first.Value + second.Value, first.GridUnitType);
    }

    private static List<WeightedDockNode> CreateSplitterInsertEntries(
        EditorDockSplitNodeViewModel targetSplit,
        out int insertIndex)
    {
        var entries = new List<WeightedDockNode>();
        var firstWeight = GetSplitWeight(targetSplit.FirstLength);
        var secondWeight = GetSplitWeight(targetSplit.SecondLength);
        CollectWeightedSplitChildren(targetSplit.First, targetSplit.Orientation, firstWeight, entries);
        insertIndex = entries.Count;
        CollectWeightedSplitChildren(targetSplit.Second, targetSplit.Orientation, secondWeight, entries);
        return entries;
    }

    private static bool TryInsertWeightedNode(
        List<WeightedDockNode> entries,
        int insertIndex,
        EditorDockWindowNodeViewModel insertedNode)
    {
        if (insertIndex <= 0 || insertIndex >= entries.Count)
        {
            return false;
        }

        var left = entries[insertIndex - 1];
        var right = entries[insertIndex];
        entries[insertIndex - 1] = left with { Weight = left.Weight * 0.5d };
        entries.Insert(insertIndex, new WeightedDockNode(insertedNode, (left.Weight + right.Weight) * 0.5d));
        entries[insertIndex + 1] = right with { Weight = right.Weight * 0.5d };
        return true;
    }

    private static bool IsSplitterInsertNoOp(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowViewModel sourceWindow)
    {
        if (sourceWindow.Tabs.Count != 1)
        {
            return false;
        }

        var entries = CreateSplitterInsertEntries(targetSplit, out var insertIndex);
        return insertIndex > 0
            && insertIndex < entries.Count
            && (IsWindowEntry(entries[insertIndex - 1], sourceWindow)
                || IsWindowEntry(entries[insertIndex], sourceWindow));
    }

    private static bool IsWindowEntry(
        WeightedDockNode entry,
        EditorDockWindowViewModel window)
    {
        return entry.Node is EditorDockWindowNodeViewModel windowNode
            && ReferenceEquals(windowNode.Window, window);
    }

    private static void CollectWeightedSplitChildren(
        EditorDockNodeViewModel node,
        Orientation orientation,
        double weight,
        List<WeightedDockNode> children)
    {
        if (node is not EditorDockSplitNodeViewModel split
            || split.Orientation != orientation
            || !HasWeightedSplitLengths(split))
        {
            children.Add(new WeightedDockNode(node, weight));
            return;
        }

        var firstWeight = GetSplitWeight(split.FirstLength);
        var secondWeight = GetSplitWeight(split.SecondLength);
        var totalWeight = firstWeight + secondWeight;
        CollectWeightedSplitChildren(split.First, orientation, weight * firstWeight / totalWeight, children);
        CollectWeightedSplitChildren(split.Second, orientation, weight * secondWeight / totalWeight, children);
    }

    private EditorDockNodeViewModel BuildWeightedSplit(
        Orientation orientation,
        IReadOnlyList<WeightedDockNode> children,
        int start,
        int count,
        out double weight)
    {
        if (count == 1)
        {
            weight = children[start].Weight;
            return children[start].Node;
        }

        var splitCount = GetWeightedSplitCount(children, start, count);
        var first = BuildWeightedSplit(orientation, children, start, splitCount, out var firstWeight);
        var second = BuildWeightedSplit(orientation, children, start + splitCount, count - splitCount, out var secondWeight);
        weight = firstWeight + secondWeight;
        return new EditorDockSplitNodeViewModel(
            CreateDynamicSplitId(),
            orientation,
            first,
            second,
            new GridLength(firstWeight, GridUnitType.Star),
            new GridLength(secondWeight, GridUnitType.Star));
    }

    private static int GetWeightedSplitCount(
        IReadOnlyList<WeightedDockNode> children,
        int start,
        int count)
    {
        var totalWeight = 0d;
        for (var index = start; index < start + count; index++)
        {
            totalWeight += children[index].Weight;
        }

        var bestCount = 1;
        var bestDistance = double.PositiveInfinity;
        var runningWeight = 0d;
        for (var splitCount = 1; splitCount < count; splitCount++)
        {
            runningWeight += children[start + splitCount - 1].Weight;
            var distance = Math.Abs((totalWeight / 2d) - runningWeight);
            if (distance >= bestDistance)
            {
                continue;
            }

            bestDistance = distance;
            bestCount = splitCount;
        }

        return bestCount;
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
                GetInsertedWindowSplitLength(),
                GetRetainedWindowSplitLength()),
            EditorDockDropOperation.InsertRight => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Horizontal,
                targetNode,
                insertedNode,
                GetRetainedWindowSplitLength(),
                GetInsertedWindowSplitLength()),
            EditorDockDropOperation.InsertTop => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Vertical,
                insertedNode,
                targetNode,
                GetInsertedWindowSplitLength(),
                GetRetainedWindowSplitLength()),
            EditorDockDropOperation.InsertBottom => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Vertical,
                targetNode,
                insertedNode,
                GetRetainedWindowSplitLength(),
                GetInsertedWindowSplitLength()),
            _ => new EditorDockSplitNodeViewModel(
                CreateDynamicSplitId(),
                Orientation.Horizontal,
                targetNode,
                insertedNode,
                GetRetainedWindowSplitLength(),
                GetInsertedWindowSplitLength()),
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

        var children = new List<WeightedDockNode>();
        CollectWeightedUserSplitChildren(split, split.Orientation, 1d, children);

        if (children.Count == 0)
        {
            return split;
        }

        if (children.Count == 1)
        {
            return children[0].Node;
        }

        return BuildWeightedSplit(split.Orientation, children, 0, children.Count, out _);
    }

    private static void CollectWeightedUserSplitChildren(
        EditorDockNodeViewModel node,
        Orientation orientation,
        double weight,
        List<WeightedDockNode> children)
    {
        if (node is EditorDockSplitNodeViewModel split
            && split.Orientation == orientation
            && IsUserSplit(split)
            && HasWeightedSplitLengths(split))
        {
            var firstWeight = GetSplitWeight(split.FirstLength);
            var secondWeight = GetSplitWeight(split.SecondLength);
            var totalWeight = firstWeight + secondWeight;
            CollectWeightedUserSplitChildren(split.First, orientation, weight * firstWeight / totalWeight, children);
            CollectWeightedUserSplitChildren(split.Second, orientation, weight * secondWeight / totalWeight, children);
            return;
        }

        children.Add(new WeightedDockNode(node, weight));
    }

    private static bool IsUserSplit(EditorDockSplitNodeViewModel split)
    {
        return split.Id.StartsWith(DynamicSplitIdPrefix, StringComparison.Ordinal);
    }

    private readonly record struct WeightedDockNode(
        EditorDockNodeViewModel Node,
        double Weight);

    private EditorDockTabViewModel CreateTab(
        PanelDescriptor descriptor,
        DockArea? initialArea = null)
    {
        return panelInstanceManager_.CreateTab(descriptor, IsFloatingWindow, initialArea);
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

}
