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
    private const string DynamicPaneIdPrefix = "owned-dock-surface-";
    private readonly Dictionary<DockArea, EditorDockPaneViewModel> panesByArea_;
    private readonly Dictionary<string, EditorDockPaneViewModel> panesById_;
    private EditorDockNodeViewModel rootNode_;
    private int nextDynamicPaneIndex_ = 1;
    private int nextDynamicSplitIndex_ = 1;

    public EditorDockWorkspaceViewModel(IPanelRegistry panelRegistry)
    {
        LeftPane = new EditorDockPaneViewModel("owned-dock-left", "Hierarchy", DockArea.Left, "Scene tree");
        CenterPane = new EditorDockPaneViewModel("owned-dock-center", "Viewport", DockArea.Center, "Primary work area");
        BottomPane = new EditorDockPaneViewModel("owned-dock-bottom", "Diagnostics", DockArea.Bottom, "Output and validation");
        RightPane = new EditorDockPaneViewModel("owned-dock-right", "Inspector", DockArea.Right, "Selection context");
        rootNode_ = CreateDefaultLayout();

        panesByArea_ = new Dictionary<DockArea, EditorDockPaneViewModel>
        {
            [DockArea.Left] = LeftPane,
            [DockArea.Center] = CenterPane,
            [DockArea.Bottom] = BottomPane,
            [DockArea.Right] = RightPane,
        };
        panesById_ = new Dictionary<string, EditorDockPaneViewModel>
        {
            [LeftPane.Id] = LeftPane,
            [CenterPane.Id] = CenterPane,
            [BottomPane.Id] = BottomPane,
            [RightPane.Id] = RightPane,
        };

        foreach (var descriptor in panelRegistry.GetAll())
        {
            var pane = panesByArea_[descriptor.DefaultArea];
            pane.Add(CreateTab(descriptor));
        }
    }

    private EditorDockWorkspaceViewModel(EditorDockPaneViewModel floatingPane)
    {
        LeftPane = floatingPane;
        CenterPane = floatingPane;
        BottomPane = floatingPane;
        RightPane = floatingPane;
        panesByArea_ = [];
        panesById_ = new Dictionary<string, EditorDockPaneViewModel>
        {
            [floatingPane.Id] = floatingPane,
        };
        nextDynamicPaneIndex_ = GetNextDynamicPaneIndex(panesById_.Values);
        rootNode_ = new EditorDockPaneNodeViewModel($"node-{floatingPane.Id}", floatingPane);
    }

    public EditorDockPaneViewModel LeftPane { get; }

    public EditorDockPaneViewModel CenterPane { get; }

    public EditorDockPaneViewModel BottomPane { get; }

    public EditorDockPaneViewModel RightPane { get; }

    public EditorDockNodeViewModel RootNode
    {
        get => rootNode_;
        private set => SetProperty(ref rootNode_, value);
    }

    public EditorDockDragStateViewModel DragState { get; } = new();

    public void BeginDrag(EditorDockTabViewModel tab, double x, double y)
    {
        FindPane(tab)?.Activate(tab);
        DragState.Begin(tab, x, y);
    }

    public void UpdateDrag(double x, double y)
    {
        DragState.UpdatePointer(x, y);
    }

    public EditorDockFloatingWindowRequest? CompleteDrag(EditorDockDropTarget target)
    {
        var tab = DragState.DraggedTab;
        if (tab is null)
        {
            DragState.Clear();
            return null;
        }

        if (target.Operation == EditorDockDropOperation.TabInto
            && target.TargetId is { } targetPaneId
            && panesById_.TryGetValue(targetPaneId, out var targetPane))
        {
            MoveTab(tab, targetPane);
        }
        else if (target.Operation == EditorDockDropOperation.SplitBetween
            && target.TargetId is { } targetSplitId)
        {
            InsertTabAtSplitter(tab, targetSplitId);
        }
        else if (IsPaneInsertOperation(target.Operation)
            && target.TargetId is { } insertTargetPaneId)
        {
            InsertTabAdjacentToPane(tab, insertTargetPaneId, target.Operation);
        }
        else if (target.Operation == EditorDockDropOperation.Float)
        {
            var request = FloatTab(tab, target.PreviewBounds);
            DragState.Clear();
            return request;
        }

        DragState.Clear();
        return null;
    }

    public void CancelDrag()
    {
        DragState.Clear();
    }

    private void MoveTab(EditorDockTabViewModel tab, EditorDockPaneViewModel targetPane)
    {
        var sourcePane = FindPane(tab);
        if (sourcePane is null)
        {
            return;
        }

        if (!ReferenceEquals(sourcePane, targetPane))
        {
            sourcePane.Remove(tab);
            targetPane.Add(tab);
        }

        targetPane.Activate(tab);
    }

    private EditorDockPaneViewModel? FindPane(EditorDockTabViewModel tab)
    {
        foreach (var pane in panesById_.Values)
        {
            if (pane.Tabs.Contains(tab))
            {
                return pane;
            }
        }

        return null;
    }

    private EditorDockFloatingWindowRequest? FloatTab(EditorDockTabViewModel tab, Avalonia.Rect bounds)
    {
        var sourcePane = FindPane(tab);
        if (sourcePane is null)
        {
            return null;
        }

        sourcePane.Remove(tab);

        var floatingPane = CreateDynamicPane(tab, sourcePane.Area);
        floatingPane.Add(tab);
        RemovePaneIfEmpty(sourcePane);
        NormalizeLayoutGraph();

        var floatingWorkspace = new EditorDockWorkspaceViewModel(floatingPane);
        var floatingWindow = new EditorDockFloatingWindowViewModel(tab.Title, floatingWorkspace);
        return new EditorDockFloatingWindowRequest(floatingWindow, bounds);
    }

    private void InsertTabAtSplitter(EditorDockTabViewModel tab, string splitId)
    {
        var sourcePane = FindPane(tab);
        var targetSplit = FindSplitNode(RootNode, splitId);
        if (sourcePane is null || targetSplit is null)
        {
            return;
        }

        sourcePane.Remove(tab);

        var insertedPane = CreateDynamicPane(tab, sourcePane.Area);
        insertedPane.Add(tab);
        panesById_.Add(insertedPane.Id, insertedPane);

        var insertedNode = new EditorDockPaneNodeViewModel(
            $"node-{insertedPane.Id}",
            insertedPane);
        InsertPaneNodeAtSplitter(targetSplit, insertedNode);

        RemovePaneIfEmpty(sourcePane);
        NormalizeLayoutGraph();
    }

    private void InsertTabAdjacentToPane(
        EditorDockTabViewModel tab,
        string targetPaneId,
        EditorDockDropOperation operation)
    {
        var sourcePane = FindPane(tab);
        if (sourcePane is null)
        {
            return;
        }

        if (!TryFindPaneNode(
                RootNode,
                targetPaneId,
                parent: null,
                out _,
                out _,
                out var targetNode)
            || targetNode is null)
        {
            return;
        }

        if (ReferenceEquals(sourcePane, targetNode.Pane) && sourcePane.Tabs.Count == 1)
        {
            return;
        }

        sourcePane.Remove(tab);

        var insertedPane = CreateDynamicPane(tab, sourcePane.Area);
        insertedPane.Add(tab);
        panesById_.Add(insertedPane.Id, insertedPane);

        var insertedNode = new EditorDockPaneNodeViewModel(
            $"node-{insertedPane.Id}",
            insertedPane);
        var replacement = CreatePaneInsertionSplit(operation, targetNode, insertedNode);

        ReplaceNode(targetNode, replacement);
        RemovePaneIfEmpty(sourcePane);
        NormalizeLayoutGraph();
    }

    private EditorDockPaneViewModel CreateDynamicPane(EditorDockTabViewModel tab, DockArea area)
    {
        var index = nextDynamicPaneIndex_++;
        return new EditorDockPaneViewModel(
            $"{DynamicPaneIdPrefix}{index}",
            tab.Title,
            area,
            "Dock surface");
    }

    private static int GetNextDynamicPaneIndex(IEnumerable<EditorDockPaneViewModel> panes)
    {
        var nextIndex = 1;
        foreach (var pane in panes)
        {
            if (!pane.Id.StartsWith(DynamicPaneIdPrefix, StringComparison.Ordinal))
            {
                continue;
            }

            var suffix = pane.Id[DynamicPaneIdPrefix.Length..];
            if (int.TryParse(suffix, out var index) && index >= nextIndex)
            {
                nextIndex = index + 1;
            }
        }

        return nextIndex;
    }

    private void InsertPaneNodeAtSplitter(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockPaneNodeViewModel insertedNode)
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

    private EditorDockSplitNodeViewModel CreatePaneInsertionSplit(
        EditorDockDropOperation operation,
        EditorDockPaneNodeViewModel targetNode,
        EditorDockPaneNodeViewModel insertedNode)
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

    private static bool IsPaneInsertOperation(EditorDockDropOperation operation)
    {
        return operation is EditorDockDropOperation.InsertLeft
            or EditorDockDropOperation.InsertRight
            or EditorDockDropOperation.InsertTop
            or EditorDockDropOperation.InsertBottom;
    }

    private EditorDockSplitNodeViewModel? FindSplitNode(EditorDockNodeViewModel node, string splitId)
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

    private void RemovePaneIfEmpty(EditorDockPaneViewModel pane)
    {
        if (pane.Tabs.Count > 0)
        {
            return;
        }

        if (!TryFindPaneNode(
                RootNode,
                pane.Id,
                parent: null,
                out var parentSplit,
                out var isFirstChild,
                out _))
        {
            return;
        }

        panesById_.Remove(pane.Id);
        if (parentSplit is null)
        {
            return;
        }

        var sibling = isFirstChild ? parentSplit.Second : parentSplit.First;
        ReplaceNode(parentSplit, sibling);
    }

    private bool TryFindPaneNode(
        EditorDockNodeViewModel node,
        string paneId,
        EditorDockSplitNodeViewModel? parent,
        out EditorDockSplitNodeViewModel? parentSplit,
        out bool isFirstChild,
        out EditorDockPaneNodeViewModel? paneNode)
    {
        if (node is EditorDockPaneNodeViewModel pane && pane.Pane.Id == paneId)
        {
            parentSplit = parent;
            isFirstChild = parent is not null && ReferenceEquals(parent.First, node);
            paneNode = pane;
            return true;
        }

        if (node is EditorDockSplitNodeViewModel split)
        {
            if (TryFindPaneNode(split.First, paneId, split, out parentSplit, out isFirstChild, out paneNode))
            {
                return true;
            }

            if (TryFindPaneNode(split.Second, paneId, split, out parentSplit, out isFirstChild, out paneNode))
            {
                return true;
            }
        }

        parentSplit = null;
        isFirstChild = false;
        paneNode = null;
        return false;
    }

    private bool ReplaceNode(EditorDockNodeViewModel target, EditorDockNodeViewModel replacement)
    {
        if (ReferenceEquals(RootNode, target))
        {
            RootNode = replacement;
            return true;
        }

        return ReplaceNode(RootNode, target, replacement);
    }

    private bool ReplaceNode(
        EditorDockNodeViewModel current,
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
        RootNode = NormalizeNode(RootNode);
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
            new EditorDockPaneNodeViewModel("node-center", CenterPane),
            new EditorDockPaneNodeViewModel("node-bottom", BottomPane),
            new GridLength(1, GridUnitType.Star),
            new GridLength(210));

        var workAndInspector = new EditorDockSplitNodeViewModel(
            "split-work-inspector",
            Orientation.Horizontal,
            centerAndBottom,
            new EditorDockPaneNodeViewModel("node-right", RightPane),
            new GridLength(1, GridUnitType.Star),
            new GridLength(320));

        return new EditorDockSplitNodeViewModel(
            "split-left-work",
            Orientation.Horizontal,
            new EditorDockPaneNodeViewModel("node-left", LeftPane),
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
