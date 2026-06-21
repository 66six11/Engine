using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Features.Hierarchy.Models;
using Editor.Shell.ViewModels;

namespace Editor.Features.Hierarchy.ViewModels;

public sealed class HierarchyPanelViewModel : ViewModelBase
{
    private const string SelectionContextId = "hierarchy";
    private readonly IEditorSelectionService selectionService_;
    private readonly Dictionary<string, HierarchyNodeModel> nodesById_;
    private readonly Dictionary<string, int> depthsByNodeId_;
    private readonly HashSet<string> nodeIdsWithChildren_;
    private readonly HashSet<string> expandedNodeIds_;
    private IReadOnlyList<HierarchyNodeRowViewModel> visibleRows_ = [];
    private HierarchyNodeModel? selectedNode_;
    private HierarchyNodeRowViewModel? selectedRow_;
    private string filterText_ = string.Empty;
    private bool hasNoMatches_;
    private string nodeCountText_ = string.Empty;

    public HierarchyPanelViewModel(IEditorSelectionService selectionService)
        : this(selectionService, new InMemorySceneSnapshotProvider(SceneSnapshot.Empty))
    {
    }

    internal HierarchyPanelViewModel(
        IEditorSelectionService selectionService,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        ArgumentNullException.ThrowIfNull(selectionService);
        ArgumentNullException.ThrowIfNull(sceneSnapshotProvider);

        selectionService_ = selectionService;
        SceneSnapshot = sceneSnapshotProvider.GetCurrentSnapshot();
        Nodes = SceneSnapshot.Objects
            .Select(HierarchyNodeModel.FromSceneObject)
            .ToArray();
        nodesById_ = Nodes.ToDictionary(node => node.Id, StringComparer.Ordinal);
        depthsByNodeId_ = Nodes.ToDictionary(node => node.Id, GetDepth, StringComparer.Ordinal);
        nodeIdsWithChildren_ = Nodes
            .Where(node => !string.IsNullOrWhiteSpace(node.ParentId))
            .Select(node => node.ParentId!)
            .ToHashSet(StringComparer.Ordinal);
        expandedNodeIds_ = Nodes
            .Where(node => node.ParentId is null && nodeIdsWithChildren_.Contains(node.Id))
            .Select(node => node.Id)
            .ToHashSet(StringComparer.Ordinal);

        ToggleExpandedCommand = new RelayCommand<HierarchyNodeRowViewModel>(ToggleExpanded);
        RefreshVisibleRows();
    }

    public SceneSnapshot SceneSnapshot { get; }

    public IReadOnlyList<HierarchyNodeModel> Nodes { get; }

    public IReadOnlyList<HierarchyNodeRowViewModel> VisibleRows
    {
        get => visibleRows_;
        private set => SetProperty(ref visibleRows_, value);
    }

    public HierarchyNodeRowViewModel? SelectedRow
    {
        get => selectedRow_;
        set
        {
            if (!SetProperty(ref selectedRow_, value))
            {
                return;
            }

            if (value is not null)
            {
                SelectedNode = value.Node;
            }
        }
    }

    public HierarchyNodeModel? SelectedNode
    {
        get => selectedNode_;
        set
        {
            if (!SetProperty(ref selectedNode_, value))
            {
                return;
            }

            if (value is null)
            {
                ClearSelection();
                SyncSelectedRow();
                return;
            }

            SelectItem(value.ToSelectionItem());
            SyncSelectedRow();
        }
    }

    public string? FilterText
    {
        get => filterText_;
        set
        {
            if (SetProperty(ref filterText_, value ?? string.Empty))
            {
                RefreshVisibleRows();
                OnPropertyChanged(nameof(HasFilter));
            }
        }
    }

    public bool HasFilter => !string.IsNullOrWhiteSpace(filterText_);

    public bool HasNoMatches
    {
        get => hasNoMatches_;
        private set => SetProperty(ref hasNoMatches_, value);
    }

    public string NodeCountText
    {
        get => nodeCountText_;
        private set => SetProperty(ref nodeCountText_, value);
    }

    public IRelayCommand<HierarchyNodeRowViewModel> ToggleExpandedCommand { get; }

    public void SelectItem(EditorSelectionItem item)
    {
        selectionService_.ReplaceSelection(SelectionContextId, [item]);
    }

    public void ClearSelection()
    {
        selectionService_.ClearSelection(SelectionContextId);
    }

    private void ToggleExpanded(HierarchyNodeRowViewModel? row)
    {
        if (row is null || !row.HasChildren)
        {
            return;
        }

        if (!expandedNodeIds_.Remove(row.Id))
        {
            expandedNodeIds_.Add(row.Id);
        }

        RefreshVisibleRows();
    }

    private void RefreshVisibleRows()
    {
        var query = filterText_.Trim();
        var hasFilter = query.Length > 0;
        var searchMatches = hasFilter
            ? Nodes
                .Where(node => MatchesQuery(node, query))
                .Select(node => node.Id)
                .ToHashSet(StringComparer.Ordinal)
            : new HashSet<string>(StringComparer.Ordinal);
        var searchAncestors = hasFilter
            ? GetAncestorIds(searchMatches)
            : new HashSet<string>(StringComparer.Ordinal);

        var visibleNodes = Nodes
            .Where(node => IsVisible(node, hasFilter, searchMatches, searchAncestors))
            .ToArray();
        var lastVisibleChildIdsByParent = GetLastVisibleChildIdsByParent(visibleNodes);

        VisibleRows = visibleNodes
            .Select(node => CreateRow(node, hasFilter, searchMatches, searchAncestors, lastVisibleChildIdsByParent))
            .ToArray();

        HasNoMatches = hasFilter && VisibleRows.Count == 0;
        NodeCountText = hasFilter
            ? $"{VisibleRows.Count}/{Nodes.Count}"
            : $"{Nodes.Count}";
        SyncSelectedRow();
    }

    private HierarchyNodeRowViewModel CreateRow(
        HierarchyNodeModel node,
        bool hasFilter,
        HashSet<string> searchMatches,
        HashSet<string> searchAncestors,
        Dictionary<string, string> lastVisibleChildIdsByParent)
    {
        var hasChildren = nodeIdsWithChildren_.Contains(node.Id);
        var isExpanded = hasChildren
            && (expandedNodeIds_.Contains(node.Id)
                || (hasFilter && searchAncestors.Contains(node.Id)));
        var isLastSibling = IsLastVisibleSibling(node, lastVisibleChildIdsByParent);

        return new HierarchyNodeRowViewModel(
            node,
            depthsByNodeId_.TryGetValue(node.Id, out var depth) ? depth : 0,
            hasChildren,
            isExpanded,
            isLastSibling,
            GetAncestorContinuationMask(node, lastVisibleChildIdsByParent),
            hasFilter && searchMatches.Contains(node.Id),
            ToggleExpandedCommand);
    }

    private static Dictionary<string, string> GetLastVisibleChildIdsByParent(
        IReadOnlyList<HierarchyNodeModel> visibleNodes)
    {
        var lastVisibleChildIdsByParent = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var node in visibleNodes)
        {
            if (!string.IsNullOrWhiteSpace(node.ParentId))
            {
                lastVisibleChildIdsByParent[node.ParentId] = node.Id;
            }
        }

        return lastVisibleChildIdsByParent;
    }

    private ulong GetAncestorContinuationMask(
        HierarchyNodeModel node,
        Dictionary<string, string> lastVisibleChildIdsByParent)
    {
        var mask = 0UL;
        var parentId = node.ParentId;
        var guard = 0;
        while (!string.IsNullOrWhiteSpace(parentId)
            && nodesById_.TryGetValue(parentId, out var parent))
        {
            if (++guard > Nodes.Count)
            {
                break;
            }

            if (depthsByNodeId_.TryGetValue(parent.Id, out var ancestorDepth)
                && ancestorDepth > 0
                && ancestorDepth <= 64
                && !IsLastVisibleSibling(parent, lastVisibleChildIdsByParent))
            {
                mask |= 1UL << (ancestorDepth - 1);
            }

            parentId = parent.ParentId;
        }

        return mask;
    }

    private static bool IsLastVisibleSibling(
        HierarchyNodeModel node,
        Dictionary<string, string> lastVisibleChildIdsByParent)
    {
        return string.IsNullOrWhiteSpace(node.ParentId)
            || (lastVisibleChildIdsByParent.TryGetValue(node.ParentId, out var lastVisibleChildId)
                && string.Equals(lastVisibleChildId, node.Id, StringComparison.Ordinal));
    }

    private bool IsVisible(
        HierarchyNodeModel node,
        bool hasFilter,
        HashSet<string> searchMatches,
        HashSet<string> searchAncestors)
    {
        if (hasFilter)
        {
            return searchMatches.Contains(node.Id) || searchAncestors.Contains(node.Id);
        }

        return AreAncestorsExpanded(node);
    }

    private bool AreAncestorsExpanded(HierarchyNodeModel node)
    {
        var parentId = node.ParentId;
        var guard = 0;
        while (!string.IsNullOrWhiteSpace(parentId))
        {
            if (++guard > Nodes.Count)
            {
                return false;
            }

            if (!expandedNodeIds_.Contains(parentId))
            {
                return false;
            }

            parentId = nodesById_.TryGetValue(parentId, out var parent)
                ? parent.ParentId
                : null;
        }

        return true;
    }

    private int GetDepth(HierarchyNodeModel node)
    {
        var depth = 0;
        var parentId = node.ParentId;
        var guard = 0;
        while (!string.IsNullOrWhiteSpace(parentId)
            && nodesById_.TryGetValue(parentId, out var parent))
        {
            if (++guard > Nodes.Count)
            {
                return depth;
            }

            depth++;
            parentId = parent.ParentId;
        }

        return depth;
    }

    private HashSet<string> GetAncestorIds(HashSet<string> nodeIds)
    {
        var ancestorIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var nodeId in nodeIds)
        {
            var parentId = nodesById_.TryGetValue(nodeId, out var node)
                ? node.ParentId
                : null;
            while (!string.IsNullOrWhiteSpace(parentId)
                && nodesById_.TryGetValue(parentId, out var parent))
            {
                if (ancestorIds.Count > Nodes.Count)
                {
                    break;
                }

                if (!ancestorIds.Add(parentId))
                {
                    break;
                }

                parentId = parent.ParentId;
            }
        }

        return ancestorIds;
    }

    private void SyncSelectedRow()
    {
        var row = selectedNode_ is null
            ? null
            : VisibleRows.FirstOrDefault(candidate => candidate.Id == selectedNode_.Id);
        SetProperty(ref selectedRow_, row, nameof(SelectedRow));
    }

    private static bool MatchesQuery(HierarchyNodeModel node, string query)
    {
        return node.DisplayName.Contains(query, StringComparison.OrdinalIgnoreCase)
            || node.Kind.Contains(query, StringComparison.OrdinalIgnoreCase)
            || node.Id.Contains(query, StringComparison.OrdinalIgnoreCase);
    }
}
