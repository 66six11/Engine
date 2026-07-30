using System;
using System.Collections.Generic;
using Asharia.Editor.Lifecycle;
using Asharia.Editor.Panels;
using Asharia.Studio.Application.Panels;
using Avalonia.Controls;
using Avalonia.Layout;
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Editor.Shell.Docking.DropTargets;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Docking.Panels;
using Editor.Shell.Lifecycle;
using Asharia.Studio.Application.Lifecycle;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Docking;

public sealed class EditorDockWorkspaceViewModel : ViewModelBase, IDisposable
{
    private const string DynamicWindowIdPrefix = "owned-dock-window-";
    private const string LayoutNodeKindSplit = "Split";
    private const string LayoutNodeKindWindow = "Window";
    private readonly IPanelRegistry? panelRegistry_;
    private readonly PanelInstanceManager panelInstanceManager_;
    private readonly bool ownsPanelInstanceManager_;
    private readonly Dictionary<EditorDockArea, EditorDockWindowViewModel> windowsByArea_;
    private readonly Dictionary<string, EditorDockWindowViewModel> windowsById_;
    private readonly Func<EditorDockLayoutSnapshot>? defaultLayoutFactory_;
    private EditorDockNodeViewModel? rootNode_;
    private EditorDockWindowViewModel? activeWindow_;
    private EditorDockTabViewModel? activeLifecycleTab_;
    private EditorDockWindowViewModel? dragSourceWindow_;
    private EditorDockTabViewModel? dragSourceTab_;
    private EditorDockWindowViewModel? tabInsertPreviewWindow_;
    private bool isHostFocused_;
    private int nextDynamicWindowIndex_ = 1;
    private int nextDynamicSplitIndex_ = 1;

    public event EventHandler? DockContentChanged;

    internal EditorPanelFrameScheduler PanelFrameScheduler { get; }

    public EditorDockWorkspaceViewModel(
        IPanelRegistry panelRegistry,
        IEditorLifecycleEventService? lifecycleEvents = null,
        EditorPanelFrameScheduler? panelFrameScheduler = null)
        : this(
            panelRegistry,
            lifecycleEvents,
            panelFrameScheduler,
            defaultLayoutFactory: null,
            initiallyFocused: true)
    {
    }

    internal EditorDockWorkspaceViewModel(
        IPanelRegistry panelRegistry,
        IEditorLifecycleEventService? lifecycleEvents,
        EditorPanelFrameScheduler? panelFrameScheduler,
        Func<EditorDockLayoutSnapshot>? defaultLayoutFactory,
        bool initiallyFocused = true)
    {
        panelRegistry_ = panelRegistry;
        defaultLayoutFactory_ = defaultLayoutFactory;
        LifecycleEvents = lifecycleEvents ?? new EditorLifecycleEventService();
        PanelFrameScheduler = panelFrameScheduler ?? new EditorPanelFrameScheduler();
        panelInstanceManager_ = new PanelInstanceManager(PanelFrameScheduler);
        ownsPanelInstanceManager_ = true;
        isHostFocused_ = initiallyFocused;
        WorkspaceKind = EditorDockWorkspaceKind.MainWindow;
        LeftWindow = new EditorDockWindowViewModel("owned-dock-left", "Hierarchy", EditorDockArea.Left, "Scene tree");
        CenterWindow = new EditorDockWindowViewModel("owned-dock-center", "Viewport", EditorDockArea.Center, "Primary work area");
        BottomWindow = new EditorDockWindowViewModel("owned-dock-bottom", "Diagnostics", EditorDockArea.Bottom, "Output and validation");
        RightWindow = new EditorDockWindowViewModel("owned-dock-right", "Inspector", EditorDockArea.Right, "Selection context");

        windowsByArea_ = new Dictionary<EditorDockArea, EditorDockWindowViewModel>
        {
            [EditorDockArea.Left] = LeftWindow,
            [EditorDockArea.Center] = CenterWindow,
            [EditorDockArea.Bottom] = BottomWindow,
            [EditorDockArea.Right] = RightWindow,
        };
        windowsById_ = new Dictionary<string, EditorDockWindowViewModel>
        {
            [LeftWindow.Id] = LeftWindow,
            [CenterWindow.Id] = CenterWindow,
            [BottomWindow.Id] = BottomWindow,
            [RightWindow.Id] = RightWindow,
        };

        try
        {
            ApplyDefaultLayout();
        }
        catch (Exception exception)
        {
            var exceptions = new CallbackExceptionBatch();
            exceptions.Add(exception);
            Dispose(exceptions);
            exceptions.ThrowIfAny();
        }
    }

    private EditorDockWorkspaceViewModel(
        EditorDockWindowViewModel floatingDockWindow,
        IEditorLifecycleEventService lifecycleEvents,
        EditorPanelFrameScheduler panelFrameScheduler,
        PanelInstanceManager panelInstanceManager,
        CallbackExceptionBatch exceptions)
    {
        panelRegistry_ = null;
        defaultLayoutFactory_ = null;
        LifecycleEvents = lifecycleEvents;
        PanelFrameScheduler = panelFrameScheduler;
        panelInstanceManager_ = panelInstanceManager;
        ownsPanelInstanceManager_ = false;
        isHostFocused_ = false;
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
        SetActiveWindow(floatingDockWindow, exceptions);
    }

    private EditorDockWorkspaceViewModel(
        IPanelRegistry panelRegistry,
        EditorDockFloatingWindowSnapshot snapshot,
        IEditorLifecycleEventService lifecycleEvents,
        EditorPanelFrameScheduler panelFrameScheduler,
        PanelInstanceManager panelInstanceManager)
    {
        panelRegistry_ = panelRegistry;
        defaultLayoutFactory_ = null;
        LifecycleEvents = lifecycleEvents;
        PanelFrameScheduler = panelFrameScheduler;
        panelInstanceManager_ = panelInstanceManager;
        ownsPanelInstanceManager_ = false;
        isHostFocused_ = false;
        WorkspaceKind = EditorDockWorkspaceKind.FloatingWindow;
        var fallbackWindow = new EditorDockWindowViewModel(
            "owned-dock-floating-restore",
            "Floating",
            EditorDockArea.Center,
            "Floating workspace");
        LeftWindow = fallbackWindow;
        CenterWindow = fallbackWindow;
        BottomWindow = fallbackWindow;
        RightWindow = fallbackWindow;
        windowsByArea_ = [];
        windowsById_ = [];

        var descriptorsById = CreatePanelDescriptorsById();
        var usedTabIds = new HashSet<string>(StringComparer.Ordinal);
        var exceptions = new CallbackExceptionBatch();
        rootNode_ = snapshot.Root is null
            ? null
            : RestoreLayoutNode(
                snapshot.Root,
                descriptorsById,
                usedTabIds,
                exceptions);
        nextDynamicWindowIndex_ = GetNextDynamicWindowIndex(windowsById_.Values);
        nextDynamicSplitIndex_ = GetNextDynamicSplitIndex(rootNode_);
        SetActiveWindow(
            snapshot.ActiveWindowId is not null
                && windowsById_.TryGetValue(snapshot.ActiveWindowId, out var activeWindow)
                    ? activeWindow
                    : FindFirstWindowWithContent(),
            exceptions);
        if (exceptions.HasExceptions)
        {
            Dispose(exceptions);
            exceptions.ThrowIfAny();
        }
    }

    public bool TryCreateFloatingWorkspace(
        EditorDockFloatingWindowSnapshot snapshot,
        out EditorDockWorkspaceViewModel workspace)
    {
        if (panelRegistry_ is null)
        {
            workspace = null!;
            return false;
        }

        workspace = new EditorDockWorkspaceViewModel(
            panelRegistry_,
            snapshot,
            LifecycleEvents,
            PanelFrameScheduler,
            panelInstanceManager_);
        if (workspace.RootNode is not null && workspace.HasDockContent())
        {
            return true;
        }

        workspace.Dispose();
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

        var exceptions = new CallbackExceptionBatch();
        ClearTransientDockState(exceptions);
        ResetWorkspaceWindows(exceptions);

        var restored = TryApplyLayoutSnapshot(snapshot, exceptions);
        if (!restored)
        {
            ResetWorkspaceWindows(exceptions);
            ApplyDefaultLayout(exceptions);
            NotifyDockContentChanged();
            exceptions.ThrowIfAny();
            return false;
        }

        NotifyDockContentChanged();
        exceptions.ThrowIfAny();
        return true;
    }

    public void ResetLayout()
    {
        if (panelRegistry_ is null)
        {
            return;
        }

        var exceptions = new CallbackExceptionBatch();
        ClearTransientDockState(exceptions);
        ResetWorkspaceWindows(exceptions);
        nextDynamicWindowIndex_ = 1;
        nextDynamicSplitIndex_ = 1;
        ApplyDefaultLayout(exceptions);
        NotifyDockContentChanged();
        exceptions.ThrowIfAny();
    }

    public bool ActivatePanel(string panelId)
    {
        if (!TryFindPanelTab(panelId, out var window, out var tab))
        {
            return false;
        }

        var exceptions = new CallbackExceptionBatch();
        window.Activate(tab, exceptions);
        SetActiveWindow(window, exceptions);
        exceptions.ThrowIfAny();
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
        var exceptions = new CallbackExceptionBatch();
        targetWindow.Add(tab, exceptions);
        targetWindow.Activate(tab, exceptions);
        SetActiveWindow(targetWindow, exceptions);
        NotifyDockContentChanged();
        exceptions.ThrowIfAny();
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

        var exceptions = new CallbackExceptionBatch();
        window.Activate(tab, exceptions);
        SetActiveWindow(window, exceptions);
        SetDragSourceState(window, tab);
        DragState.Begin(tab);
        exceptions.ThrowIfAny();
    }

    public bool ActivateTab(EditorDockTabViewModel tab)
    {
        var window = FindWindow(tab);
        if (window is null)
        {
            return false;
        }

        var exceptions = new CallbackExceptionBatch();
        window.Activate(tab, exceptions);
        SetActiveWindow(window, exceptions);
        exceptions.ThrowIfAny();
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
        var exceptions = new CallbackExceptionBatch();
        window.Activate(tab, exceptions);
        SetActiveWindow(window, exceptions);
        exceptions.ThrowIfAny();
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

        var exceptions = new CallbackExceptionBatch();
        UpdateActivePanelLifecycle(exceptions);
        exceptions.ThrowIfAny();
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

            if (!ReferenceEquals(
                    panelInstanceManager_,
                    targetWorkspace.panelInstanceManager_))
            {
                return null;
            }

            if (!targetWorkspace.CanAcceptDetachedTab(target))
            {
                return null;
            }

            var sourceArea = sourceWindow.Area;
            MoveDetachedTabInto(
                tab,
                sourceWindow,
                targetWorkspace,
                target,
                sourceArea);
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

        var exceptions = new CallbackExceptionBatch();
        sourceWindow.Remove(tab, exceptions);
        tab.ReleasePanelInstance(exceptions);
        RemoveWindowIfEmpty(sourceWindow, exceptions);
        NormalizeLayoutGraph();
        SetActiveWindow(
            sourceWindow.Tabs.Count > 0
                ? sourceWindow
                : FindFirstWindowWithContent(),
            exceptions);
        NotifyDockContentChanged();
        exceptions.ThrowIfAny();
        return true;
    }

    public bool ClosePanel(string panelId)
    {
        return TryFindPanelTab(panelId, out _, out var tab)
            && CloseTab(tab);
    }

    public void Dispose()
    {
        var exceptions = new CallbackExceptionBatch();
        Dispose(exceptions);
        exceptions.ThrowIfAny();
    }

    private void Dispose(CallbackExceptionBatch exceptions)
    {
        ResetWorkspaceWindows(exceptions);
        if (ownsPanelInstanceManager_)
        {
            panelInstanceManager_.Dispose(exceptions);
        }
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
        HashSet<string> usedTabIds,
        CallbackExceptionBatch exceptions)
    {
        if (snapshot.Kind == LayoutNodeKindSplit)
        {
            var first = snapshot.First is null
                ? null
                : RestoreLayoutNode(
                    snapshot.First,
                    descriptorsById,
                    usedTabIds,
                    exceptions);
            var second = snapshot.Second is null
                ? null
                : RestoreLayoutNode(
                    snapshot.Second,
                    descriptorsById,
                    usedTabIds,
                    exceptions);
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

        var window = RestoreWindow(
            snapshot,
            descriptorsById,
            usedTabIds,
            exceptions);
        return window is null
            ? null
            : new EditorDockWindowNodeViewModel(
                string.IsNullOrWhiteSpace(snapshot.Id) ? $"node-{window.Id}" : snapshot.Id,
                window);
    }

    private EditorDockWindowViewModel? RestoreWindow(
        EditorDockLayoutNodeSnapshot snapshot,
        IReadOnlyDictionary<string, PanelDescriptor> descriptorsById,
        HashSet<string> usedTabIds,
        CallbackExceptionBatch exceptions)
    {
        var tabs = new List<EditorDockTabViewModel>();
        try
        {
            foreach (var tabId in snapshot.TabIds)
            {
                if (descriptorsById.TryGetValue(tabId, out var descriptor)
                    && usedTabIds.Add(tabId))
                {
                    tabs.Add(CreateTab(descriptor, snapshot.WindowArea));
                }
            }
        }
        catch (Exception exception)
        {
            exceptions.Add(exception);
            foreach (var tab in tabs)
            {
                tab.ReleasePanelInstance(exceptions);
            }

            exceptions.ThrowIfAny();
            throw;
        }

        if (tabs.Count == 0)
        {
            return null;
        }

        var window = GetOrCreateRestoredWindow(snapshot, tabs[0]);
        foreach (var tab in tabs)
        {
            window.Add(tab, exceptions);
        }

        if (snapshot.ActiveTabId is not null)
        {
            foreach (var tab in window.Tabs)
            {
                if (tab.Id == snapshot.ActiveTabId)
                {
                    window.Activate(tab, exceptions);
                    return window;
                }
            }
        }

        window.Activate(window.Tabs[0], exceptions);
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

    private void ApplyDefaultLayout()
    {
        var exceptions = new CallbackExceptionBatch();
        ApplyDefaultLayout(exceptions);
        exceptions.ThrowIfAny();
    }

    private void ApplyDefaultLayout(CallbackExceptionBatch exceptions)
    {
        if (defaultLayoutFactory_ is not null
            && TryApplyLayoutSnapshot(defaultLayoutFactory_(), exceptions))
        {
            return;
        }

        RootNode = CreateDefaultLayout();
        foreach (var descriptor in panelRegistry_?.GetAll() ?? [])
        {
            var window = windowsByArea_[descriptor.DefaultArea];
            window.Add(CreateTab(descriptor, window.Area), exceptions);
        }

        SetActiveWindow(
            CenterWindow.Tabs.Count > 0
                ? CenterWindow
                : FindFirstWindowWithContent(),
            exceptions);
    }

    private bool TryApplyLayoutSnapshot(
        EditorDockLayoutSnapshot? snapshot,
        CallbackExceptionBatch exceptions)
    {
        if (panelRegistry_ is null
            || snapshot?.Root is null
            || snapshot.Version != 1)
        {
            return false;
        }

        var descriptorsById = CreatePanelDescriptorsById();
        var usedTabIds = new HashSet<string>(StringComparer.Ordinal);
        var restoredRoot = RestoreLayoutNode(
            snapshot.Root,
            descriptorsById,
            usedTabIds,
            exceptions);
        if (restoredRoot is null)
        {
            return false;
        }

        RootNode = restoredRoot;
        nextDynamicWindowIndex_ = GetNextDynamicWindowIndex(windowsById_.Values);
        nextDynamicSplitIndex_ = GetNextDynamicSplitIndex(RootNode);
        SetActiveWindow(
            snapshot.ActiveWindowId is not null
                && windowsById_.TryGetValue(snapshot.ActiveWindowId, out var activeWindow)
                    ? activeWindow
                    : FindFirstWindowWithContent(),
            exceptions);
        return true;
    }

    private void ResetWorkspaceWindows(CallbackExceptionBatch exceptions)
    {
        SetActivePanelLifecycle(null, exceptions);
        var existingWindows = new List<EditorDockWindowViewModel>(windowsById_.Values);
        foreach (var window in existingWindows)
        {
            ReleaseWindowTabs(window, exceptions);
            window.ResetTabs(exceptions);
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

    private void ClearTransientDockState(CallbackExceptionBatch exceptions)
    {
        ClearTabInsertPreview();
        ClearDragSourceState();
        DragState.Clear();
        SetActivePanelLifecycle(null, exceptions);
        if (activeWindow_ is not null)
        {
            activeWindow_.SetActiveWindowState(false);
        }

        activeWindow_ = null;
    }

    private static void ReleaseWindowTabs(
        EditorDockWindowViewModel window,
        CallbackExceptionBatch exceptions)
    {
        var tabs = new List<EditorDockTabViewModel>(window.Tabs);
        foreach (var tab in tabs)
        {
            tab.ReleasePanelInstance(exceptions);
        }
    }

    private void MoveTab(EditorDockTabViewModel tab, EditorDockWindowViewModel targetWindow)
    {
        var sourceWindow = FindWindow(tab);
        if (sourceWindow is null)
        {
            return;
        }

        var exceptions = new CallbackExceptionBatch();
        if (!ReferenceEquals(sourceWindow, targetWindow))
        {
            sourceWindow.Remove(tab, exceptions);
            targetWindow.Add(tab, exceptions);
        }

        targetWindow.Activate(tab, exceptions);
        SetActiveWindow(targetWindow, exceptions);
        exceptions.ThrowIfAny();
    }

    private void MoveDetachedTabInto(
        EditorDockTabViewModel tab,
        EditorDockWindowViewModel sourceWindow,
        EditorDockWorkspaceViewModel targetWorkspace,
        EditorDockDropTarget target,
        EditorDockArea sourceArea)
    {
        var exceptions = new CallbackExceptionBatch();
        sourceWindow.Remove(tab, exceptions);
        SetActiveWindow(
            sourceWindow.Tabs.Count > 0
                ? sourceWindow
                : FindFirstWindowWithContent(),
            exceptions);
        targetWorkspace.InsertDetachedTab(
            tab,
            target,
            sourceArea,
            exceptions);
        RemoveWindowIfEmpty(sourceWindow, exceptions);
        NormalizeLayoutGraph();
        exceptions.ThrowIfAny();
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

        var exceptions = new CallbackExceptionBatch();
        sourceWindow.Remove(tab, exceptions);
        targetWindow.Insert(tab, targetIndex, exceptions);
        targetWindow.Activate(tab, exceptions);
        SetActiveWindow(targetWindow, exceptions);
        RemoveWindowIfEmpty(sourceWindow, exceptions);
        NormalizeLayoutGraph();
        exceptions.ThrowIfAny();
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
        var exceptions = new CallbackExceptionBatch();
        targetWindow.Activate(tab, exceptions);
        SetActiveWindow(targetWindow, exceptions);
        exceptions.ThrowIfAny();
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

    private EditorDockWindowViewModel? GetPanelOpenTargetWindow(EditorDockArea defaultArea)
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

        if (window.Area == EditorDockArea.Center && TryRestoreCenterWindow(insertedNode))
        {
            return;
        }

        InsertWindowNodeAtWorkspaceEdge(GetWorkspaceEdgeOperation(window.Area), insertedNode);
    }

    private bool TryRestoreCenterWindow(EditorDockWindowNodeViewModel insertedNode)
    {
        return TryInsertPrimaryWindowAdjacentTo(EditorDockArea.Bottom, EditorDockDropOperation.InsertTop, insertedNode)
            || TryInsertPrimaryWindowAdjacentTo(EditorDockArea.Right, EditorDockDropOperation.InsertLeft, insertedNode)
            || TryInsertPrimaryWindowAdjacentTo(EditorDockArea.Left, EditorDockDropOperation.InsertRight, insertedNode);
    }

    private bool TryInsertPrimaryWindowAdjacentTo(
        EditorDockArea targetArea,
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

    private static string GetPrimaryWindowNodeId(EditorDockArea area)
    {
        return area switch
        {
            EditorDockArea.Left => "node-left",
            EditorDockArea.Center => "node-center",
            EditorDockArea.Bottom => "node-bottom",
            EditorDockArea.Right => "node-right",
            _ => $"node-{area.ToString().ToLowerInvariant()}",
        };
    }

    private static EditorDockDropOperation GetWorkspaceEdgeOperation(EditorDockArea area)
    {
        return area switch
        {
            EditorDockArea.Left => EditorDockDropOperation.InsertWorkspaceLeft,
            EditorDockArea.Right => EditorDockDropOperation.InsertWorkspaceRight,
            EditorDockArea.Bottom => EditorDockDropOperation.InsertWorkspaceBottom,
            _ => EditorDockDropOperation.InsertWorkspaceTop,
        };
    }

    private void SetActiveWindow(
        EditorDockWindowViewModel? window,
        CallbackExceptionBatch exceptions)
    {
        if (ReferenceEquals(activeWindow_, window))
        {
            UpdateActivePanelLifecycle(exceptions);
            return;
        }

        activeWindow_?.SetActiveWindowState(false);
        activeWindow_ = window;
        activeWindow_?.SetActiveWindowState(true);
        UpdateActivePanelLifecycle(exceptions);
        OnPropertyChanged(nameof(ActiveWindow));
        OnPropertyChanged(nameof(ActiveWindowTitle));
        OnPropertyChanged(nameof(HostTitle));
    }

    private void UpdateActivePanelLifecycle(CallbackExceptionBatch exceptions)
    {
        SetActivePanelLifecycle(
            IsHostFocused
                ? activeWindow_?.ActiveTab
                : null,
            exceptions);
    }

    private void SetActivePanelLifecycle(
        EditorDockTabViewModel? tab,
        CallbackExceptionBatch exceptions)
    {
        if (ReferenceEquals(activeLifecycleTab_, tab))
        {
            activeLifecycleTab_?.ActivatePanelInstance(exceptions);
            return;
        }

        if (activeLifecycleTab_ is not null)
        {
            activeLifecycleTab_.DeactivatePanelInstance(exceptions);
        }

        activeLifecycleTab_ = tab;
        if (activeLifecycleTab_ is not null)
        {
            activeLifecycleTab_.ActivatePanelInstance(exceptions);
        }
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

        var sourceIndex = sourceWindow.Tabs.IndexOf(tab);
        var exceptions = new CallbackExceptionBatch();
        sourceWindow.Remove(tab, exceptions);
        SetActiveWindow(
            sourceWindow.Tabs.Count > 0
                ? sourceWindow
                : FindFirstWindowWithContent(),
            exceptions);
        if (exceptions.HasExceptions)
        {
            RestoreTabAfterFailedFloat(
                tab,
                sourceWindow,
                sourceIndex,
                floatingDockWindow: null,
                floatingWorkspace: null,
                exceptions);
            exceptions.ThrowIfAny();
        }

        var floatingDockWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        tab.SetPanelLifecycleHostKind(isFloatingWorkspace: true);
        floatingDockWindow.Add(tab, exceptions);
        if (exceptions.HasExceptions)
        {
            RestoreTabAfterFailedFloat(
                tab,
                sourceWindow,
                sourceIndex,
                floatingDockWindow,
                floatingWorkspace: null,
                exceptions);
            exceptions.ThrowIfAny();
        }

        var floatingWorkspace = new EditorDockWorkspaceViewModel(
            floatingDockWindow,
            LifecycleEvents,
            PanelFrameScheduler,
            panelInstanceManager_,
            exceptions);
        if (exceptions.HasExceptions)
        {
            RestoreTabAfterFailedFloat(
                tab,
                sourceWindow,
                sourceIndex,
                floatingDockWindow,
                floatingWorkspace,
                exceptions);
            exceptions.ThrowIfAny();
        }

        RemoveWindowIfEmpty(sourceWindow, exceptions);
        NormalizeLayoutGraph();
        var floatingWindow = new EditorDockFloatingWindowViewModel(floatingWorkspace, LifecycleEvents);
        return new EditorDockFloatingWindowRequest(floatingWindow, bounds);
    }

    private void RestoreTabAfterFailedFloat(
        EditorDockTabViewModel tab,
        EditorDockWindowViewModel sourceWindow,
        int sourceIndex,
        EditorDockWindowViewModel? floatingDockWindow,
        EditorDockWorkspaceViewModel? floatingWorkspace,
        CallbackExceptionBatch exceptions)
    {
        if (floatingWorkspace is not null)
        {
            floatingWorkspace.SetActiveWindow(null, exceptions);
        }

        floatingDockWindow?.Remove(tab, exceptions);
        floatingWorkspace?.Dispose(exceptions);
        tab.SetPanelLifecycleHostKind(IsFloatingWindow);
        sourceWindow.Insert(tab, sourceIndex, exceptions);
        sourceWindow.Activate(tab, exceptions);
        SetActiveWindow(sourceWindow, exceptions);
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
            var noOpExceptions = new CallbackExceptionBatch();
            sourceWindow.Activate(tab, noOpExceptions);
            SetActiveWindow(sourceWindow, noOpExceptions);
            noOpExceptions.ThrowIfAny();
            return;
        }

        var exceptions = new CallbackExceptionBatch();
        sourceWindow.Remove(tab, exceptions);

        var insertedWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        insertedWindow.Add(tab, exceptions);
        windowsById_.Add(insertedWindow.Id, insertedWindow);

        var insertedNode = new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
        InsertWindowNodeAtSplitter(targetSplit, insertedNode, target);

        RemoveWindowIfEmpty(sourceWindow, exceptions);
        SetActiveWindow(insertedWindow, exceptions);
        exceptions.ThrowIfAny();
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

        var exceptions = new CallbackExceptionBatch();
        sourceWindow.Remove(tab, exceptions);

        var insertedWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        insertedWindow.Add(tab, exceptions);
        windowsById_.Add(insertedWindow.Id, insertedWindow);

        var insertedNode = new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
        var replacement = CreateWindowInsertionSplit(operation, targetNode, insertedNode);

        ReplaceNode(targetNode, replacement);
        RemoveWindowIfEmpty(sourceWindow, exceptions);
        NormalizeLayoutGraph();
        SetActiveWindow(insertedWindow, exceptions);
        exceptions.ThrowIfAny();
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

        var exceptions = new CallbackExceptionBatch();
        sourceWindow.Remove(tab, exceptions);

        var insertedWindow = CreateDynamicWindow(tab, sourceWindow.Area);
        insertedWindow.Add(tab, exceptions);
        windowsById_.Add(insertedWindow.Id, insertedWindow);

        var insertedNode = new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
        InsertWindowNodeAtWorkspaceEdge(operation, insertedNode);

        RemoveWindowIfEmpty(sourceWindow, exceptions);
        NormalizeLayoutGraph();
        SetActiveWindow(insertedWindow, exceptions);
        exceptions.ThrowIfAny();
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
        EditorDockArea fallbackArea,
        CallbackExceptionBatch exceptions)
    {
        tab.SetPanelFrameScheduler(PanelFrameScheduler);
        tab.SetPanelLifecycleHostKind(IsFloatingWindow);
        if (target.Operation == EditorDockDropOperation.TabInto
            && target.TargetId is { } targetWindowId
            && windowsById_.TryGetValue(targetWindowId, out var targetWindow))
        {
            targetWindow.Add(tab, exceptions);
            targetWindow.Activate(tab, exceptions);
            SetActiveWindow(targetWindow, exceptions);
            return;
        }

        if (target.Operation == EditorDockDropOperation.InsertTabAtIndex
            && target.TargetId is { } tabInsertTargetWindowId
            && target.TargetIndex is { } tabInsertTargetIndex
            && windowsById_.TryGetValue(tabInsertTargetWindowId, out var tabInsertTargetWindow))
        {
            tabInsertTargetWindow.Insert(
                tab,
                tabInsertTargetIndex,
                exceptions);
            tabInsertTargetWindow.Activate(tab, exceptions);
            SetActiveWindow(tabInsertTargetWindow, exceptions);
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

            var insertedNode = CreateDetachedWindowNode(
                tab,
                fallbackArea,
                exceptions);
            InsertWindowNodeAtSplitter(targetSplit, insertedNode, target);
            return;
        }

        if (IsWorkspaceEdgeInsertOperation(target.Operation))
        {
            InsertDetachedTabAtWorkspaceEdge(
                tab,
                target,
                fallbackArea,
                exceptions);
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
            var insertedNode = CreateDetachedWindowNode(
                tab,
                fallbackArea,
                exceptions);
            var replacement = CreateWindowInsertionSplit(target.Operation, targetNode, insertedNode);
            ReplaceNode(targetNode, replacement);
            NormalizeLayoutGraph();
        }
    }

    private void InsertDetachedTabAtWorkspaceEdge(
        EditorDockTabViewModel tab,
        EditorDockDropTarget target,
        EditorDockArea fallbackArea,
        CallbackExceptionBatch exceptions)
    {
        var insertedNode = CreateDetachedWindowNode(
            tab,
            fallbackArea,
            exceptions);
        InsertWindowNodeAtWorkspaceEdge(target.Operation, insertedNode);
    }

    private EditorDockWindowNodeViewModel CreateDetachedWindowNode(
        EditorDockTabViewModel tab,
        EditorDockArea fallbackArea,
        CallbackExceptionBatch exceptions)
    {
        var insertedWindow = CreateDynamicWindow(tab, fallbackArea);
        insertedWindow.Add(tab, exceptions);
        windowsById_.Add(insertedWindow.Id, insertedWindow);
        SetActiveWindow(insertedWindow, exceptions);
        return new EditorDockWindowNodeViewModel(
            $"node-{insertedWindow.Id}",
            insertedWindow);
    }

    private EditorDockWindowViewModel CreateDynamicWindow(
        EditorDockTabViewModel tab,
        EditorDockArea area)
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
        return EditorDockLayoutGraph.GetNextDynamicSplitIndex(node);
    }

    private void InsertWindowNodeAtSplitter(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        EditorDockDropTarget target)
    {
        RootNode = EditorDockLayoutGraph.InsertWindowNodeAtSplitter(
            RootNode,
            targetSplit,
            insertedNode,
            target,
            CreateDynamicSplitId);
    }

    private void InsertWindowNodeAtWorkspaceEdge(
        EditorDockDropOperation operation,
        EditorDockWindowNodeViewModel insertedNode)
    {
        RootNode = EditorDockLayoutGraph.InsertWindowNodeAtWorkspaceEdge(
            RootNode,
            operation,
            insertedNode,
            CreateDynamicSplitId);
    }

    private string CreateDynamicSplitId()
    {
        return $"{EditorDockLayoutGraph.DynamicSplitIdPrefix}{nextDynamicSplitIndex_++}";
    }

    private static bool IsSplitterInsertNoOp(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowViewModel sourceWindow)
    {
        return EditorDockLayoutGraph.IsSplitterInsertNoOp(targetSplit, sourceWindow);
    }

    private EditorDockSplitNodeViewModel CreateWindowInsertionSplit(
        EditorDockDropOperation operation,
        EditorDockWindowNodeViewModel targetNode,
        EditorDockWindowNodeViewModel insertedNode)
    {
        return EditorDockLayoutGraph.CreateWindowInsertionSplit(
            operation,
            targetNode,
            insertedNode,
            CreateDynamicSplitId);
    }

    private static bool IsWindowInsertOperation(EditorDockDropOperation operation)
    {
        return EditorDockLayoutGraph.IsWindowInsertOperation(operation);
    }

    private static bool IsWorkspaceEdgeInsertOperation(EditorDockDropOperation operation)
    {
        return EditorDockLayoutGraph.IsWorkspaceEdgeInsertOperation(operation);
    }

    private static EditorDockSplitNodeViewModel? FindSplitNode(
        EditorDockNodeViewModel? node,
        string splitId)
    {
        return EditorDockLayoutGraph.FindSplitNode(node, splitId);
    }

    private void RemoveWindowIfEmpty(
        EditorDockWindowViewModel window,
        CallbackExceptionBatch exceptions)
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
                SetActiveWindow(null, exceptions);
            }

            return;
        }

        var sibling = isFirstChild ? parentSplit.Second : parentSplit.First;
        ReplaceNode(parentSplit, sibling);
        if (isActiveWindow)
        {
            SetActiveWindow(FindFirstWindowWithContent(), exceptions);
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
        return EditorDockLayoutGraph.TryFindWindowNode(
            node,
            windowId,
            parent,
            out parentSplit,
            out isFirstChild,
            out windowNode);
    }

    private bool ReplaceNode(EditorDockNodeViewModel target, EditorDockNodeViewModel replacement)
    {
        RootNode = EditorDockLayoutGraph.ReplaceNode(
            RootNode,
            target,
            replacement,
            out var replaced);
        return replaced;
    }

    private void NormalizeLayoutGraph()
    {
        RootNode = EditorDockLayoutGraph.Normalize(RootNode, CreateDynamicSplitId);
    }

    private EditorDockTabViewModel CreateTab(
        PanelDescriptor descriptor,
        EditorDockArea? initialArea = null)
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
