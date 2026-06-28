using System;

namespace Editor.UI.Controls.Tree;

public sealed record EditorTreeRow<TPayload>
{
    public EditorTreeRow(
        EditorTreeNode<TPayload> node,
        int depth,
        bool hasChildren,
        bool isExpanded,
        bool isLastSibling,
        ulong ancestorContinuationMask,
        bool isSearchMatch)
    {
        ArgumentNullException.ThrowIfNull(node);

        Node = node;
        Depth = Math.Max(0, depth);
        HasChildren = hasChildren;
        IsExpanded = isExpanded;
        IsLastSibling = isLastSibling;
        AncestorContinuationMask = ancestorContinuationMask;
        IsSearchMatch = isSearchMatch;
    }

    public EditorTreeNode<TPayload> Node { get; }

    public string Id => Node.Id;

    public TPayload Payload => Node.Payload;

    public int Depth { get; }

    public bool HasChildren { get; }

    public bool IsExpanded { get; }

    public bool IsLastSibling { get; }

    public ulong AncestorContinuationMask { get; }

    public bool IsSearchMatch { get; }
}
