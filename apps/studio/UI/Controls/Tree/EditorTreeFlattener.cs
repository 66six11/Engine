using System;
using System.Collections.Generic;
using System.Linq;

namespace Editor.UI.Controls.Tree;

public static class EditorTreeFlattener
{
    public static IReadOnlyList<EditorTreeRow<TPayload>> Flatten<TPayload>(
        IReadOnlyList<EditorTreeNode<TPayload>> nodes,
        EditorTreeExpansionState expansionState,
        Func<EditorTreeNode<TPayload>, bool>? searchPredicate = null)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(expansionState);

        var nodesById = BuildNodeIndex(nodes);
        var nodeIdsWithChildren = nodes
            .Where(node => !string.IsNullOrWhiteSpace(node.ParentId))
            .Select(node => node.ParentId!)
            .ToHashSet(StringComparer.Ordinal);
        var depthsByNodeId = nodes.ToDictionary(
            node => node.Id,
            node => GetDepth(node, nodesById, nodes.Count),
            StringComparer.Ordinal);
        var hasSearch = searchPredicate is not null;
        var searchMatches = hasSearch
            ? nodes.Where(searchPredicate!).Select(node => node.Id).ToHashSet(StringComparer.Ordinal)
            : new HashSet<string>(StringComparer.Ordinal);
        var searchAncestors = hasSearch
            ? GetAncestorIds(searchMatches, nodesById, nodes.Count)
            : new HashSet<string>(StringComparer.Ordinal);
        var visibleNodes = nodes
            .Where(node => IsVisible(
                node,
                hasSearch,
                searchMatches,
                searchAncestors,
                expansionState,
                nodesById,
                nodes.Count))
            .ToArray();
        var lastVisibleChildIdsByParent = GetLastVisibleChildIdsByParent(visibleNodes);

        return visibleNodes
            .Select(node => new EditorTreeRow<TPayload>(
                node,
                depthsByNodeId.TryGetValue(node.Id, out var depth) ? depth : 0,
                nodeIdsWithChildren.Contains(node.Id),
                nodeIdsWithChildren.Contains(node.Id)
                    && (expansionState.IsExpanded(node.Id)
                        || (hasSearch && searchAncestors.Contains(node.Id))),
                IsLastVisibleSibling(node, lastVisibleChildIdsByParent),
                GetAncestorContinuationMask(node, nodesById, depthsByNodeId, lastVisibleChildIdsByParent, nodes.Count),
                hasSearch && searchMatches.Contains(node.Id)))
            .ToArray();
    }

    private static Dictionary<string, EditorTreeNode<TPayload>> BuildNodeIndex<TPayload>(
        IReadOnlyList<EditorTreeNode<TPayload>> nodes)
    {
        var nodesById = new Dictionary<string, EditorTreeNode<TPayload>>(StringComparer.Ordinal);
        foreach (var node in nodes)
        {
            if (!nodesById.TryAdd(node.Id, node))
            {
                throw new InvalidOperationException($"Tree contains duplicate node id '{node.Id}'.");
            }
        }

        return nodesById;
    }

    private static bool IsVisible<TPayload>(
        EditorTreeNode<TPayload> node,
        bool hasSearch,
        HashSet<string> searchMatches,
        HashSet<string> searchAncestors,
        EditorTreeExpansionState expansionState,
        Dictionary<string, EditorTreeNode<TPayload>> nodesById,
        int nodeCount)
    {
        if (hasSearch)
        {
            return searchMatches.Contains(node.Id) || searchAncestors.Contains(node.Id);
        }

        return AreAncestorsExpanded(node, expansionState, nodesById, nodeCount);
    }

    private static bool AreAncestorsExpanded<TPayload>(
        EditorTreeNode<TPayload> node,
        EditorTreeExpansionState expansionState,
        Dictionary<string, EditorTreeNode<TPayload>> nodesById,
        int nodeCount)
    {
        var parentId = node.ParentId;
        var guard = 0;
        while (!string.IsNullOrWhiteSpace(parentId))
        {
            if (++guard > nodeCount)
            {
                return false;
            }

            if (!expansionState.IsExpanded(parentId))
            {
                return false;
            }

            parentId = nodesById.TryGetValue(parentId, out var parent)
                ? parent.ParentId
                : null;
        }

        return true;
    }

    private static int GetDepth<TPayload>(
        EditorTreeNode<TPayload> node,
        Dictionary<string, EditorTreeNode<TPayload>> nodesById,
        int nodeCount)
    {
        var depth = 0;
        var parentId = node.ParentId;
        var guard = 0;
        while (!string.IsNullOrWhiteSpace(parentId)
            && nodesById.TryGetValue(parentId, out var parent))
        {
            if (++guard > nodeCount)
            {
                return depth;
            }

            depth++;
            parentId = parent.ParentId;
        }

        return depth;
    }

    private static HashSet<string> GetAncestorIds<TPayload>(
        HashSet<string> nodeIds,
        Dictionary<string, EditorTreeNode<TPayload>> nodesById,
        int nodeCount)
    {
        var ancestorIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var nodeId in nodeIds)
        {
            var parentId = nodesById.TryGetValue(nodeId, out var node)
                ? node.ParentId
                : null;
            while (!string.IsNullOrWhiteSpace(parentId)
                && nodesById.TryGetValue(parentId, out var parent))
            {
                if (ancestorIds.Count > nodeCount)
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

    private static Dictionary<string, string> GetLastVisibleChildIdsByParent<TPayload>(
        IReadOnlyList<EditorTreeNode<TPayload>> visibleNodes)
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

    private static bool IsLastVisibleSibling<TPayload>(
        EditorTreeNode<TPayload> node,
        Dictionary<string, string> lastVisibleChildIdsByParent)
    {
        return string.IsNullOrWhiteSpace(node.ParentId)
            || (lastVisibleChildIdsByParent.TryGetValue(node.ParentId, out var lastVisibleChildId)
                && string.Equals(lastVisibleChildId, node.Id, StringComparison.Ordinal));
    }

    private static ulong GetAncestorContinuationMask<TPayload>(
        EditorTreeNode<TPayload> node,
        Dictionary<string, EditorTreeNode<TPayload>> nodesById,
        Dictionary<string, int> depthsByNodeId,
        Dictionary<string, string> lastVisibleChildIdsByParent,
        int nodeCount)
    {
        var mask = 0UL;
        var parentId = node.ParentId;
        var guard = 0;
        while (!string.IsNullOrWhiteSpace(parentId)
            && nodesById.TryGetValue(parentId, out var parent))
        {
            if (++guard > nodeCount)
            {
                break;
            }

            if (depthsByNodeId.TryGetValue(parent.Id, out var ancestorDepth)
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
}
