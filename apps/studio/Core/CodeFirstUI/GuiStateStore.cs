using System;
using System.Collections.Generic;

namespace Editor.Core.CodeFirstUI;

public sealed class GuiStateStore
{
    private readonly Dictionary<GuiNodeId, string> selectedItemsByNode_ = [];
    private readonly Dictionary<GuiNodeId, string> textByNode_ = [];

    public void SetText(GuiNodeId nodeId, string text)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        textByNode_[nodeId] = text;
    }

    public bool TryGetText(GuiNodeId nodeId, out string? text)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return textByNode_.TryGetValue(nodeId, out text);
    }

    public void SetSelectedItem(GuiNodeId nodeId, string itemId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        selectedItemsByNode_[nodeId] = itemId;
    }

    public bool TryGetSelectedItem(GuiNodeId nodeId, out string? itemId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return selectedItemsByNode_.TryGetValue(nodeId, out itemId);
    }

    public void ClearNodeState(GuiNodeId nodeId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        textByNode_.Remove(nodeId);
        selectedItemsByNode_.Remove(nodeId);
    }
}
