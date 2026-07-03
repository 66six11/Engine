using System;

namespace Editor.UI.Controls.Tree;

public sealed record EditorTreeNode<TPayload>
{
    public EditorTreeNode(
        string id,
        string? parentId,
        TPayload payload)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            throw new ArgumentException("Tree node id must not be empty.", nameof(id));
        }

        Id = id;
        ParentId = string.IsNullOrWhiteSpace(parentId) ? null : parentId;
        Payload = payload;
    }

    public string Id { get; }

    public string? ParentId { get; }

    public TPayload Payload { get; }
}
