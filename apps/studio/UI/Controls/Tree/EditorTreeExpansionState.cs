using System;
using System.Collections.Generic;

namespace Editor.UI.Controls.Tree;

public sealed class EditorTreeExpansionState
{
    private readonly HashSet<string> expandedNodeIds_;

    public EditorTreeExpansionState()
        : this([])
    {
    }

    public EditorTreeExpansionState(IEnumerable<string> expandedNodeIds)
    {
        ArgumentNullException.ThrowIfNull(expandedNodeIds);

        expandedNodeIds_ = new HashSet<string>(
            expandedNodeIds,
            StringComparer.Ordinal);
    }

    public bool IsExpanded(string nodeId)
    {
        return !string.IsNullOrWhiteSpace(nodeId)
            && expandedNodeIds_.Contains(nodeId);
    }

    public void SetExpanded(
        string nodeId,
        bool isExpanded,
        bool hasChildren)
    {
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            throw new ArgumentException("Tree node id must not be empty.", nameof(nodeId));
        }

        if (!hasChildren)
        {
            expandedNodeIds_.Remove(nodeId);
            return;
        }

        if (isExpanded)
        {
            expandedNodeIds_.Add(nodeId);
            return;
        }

        expandedNodeIds_.Remove(nodeId);
    }

    public bool Toggle(string nodeId, bool hasChildren)
    {
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            throw new ArgumentException("Tree node id must not be empty.", nameof(nodeId));
        }

        if (!hasChildren)
        {
            expandedNodeIds_.Remove(nodeId);
            return false;
        }

        if (!expandedNodeIds_.Remove(nodeId))
        {
            expandedNodeIds_.Add(nodeId);
        }

        return true;
    }

    public HashSet<string> ToHashSet()
    {
        return new HashSet<string>(expandedNodeIds_, StringComparer.Ordinal);
    }
}
