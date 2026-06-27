using System;
using System.Collections.Generic;

namespace Editor.Core.CodeFirstUI;

public sealed class GuiStateStore
{
    private readonly Dictionary<GuiNodeId, string> selectedItemsByNode_ = [];
    private readonly Dictionary<GuiNodeId, double> splitRatiosByNode_ = [];
    private readonly Dictionary<GuiNodeId, string> textByNode_ = [];
    private readonly Dictionary<GuiNodeId, bool> togglesByNode_ = [];
    private readonly Dictionary<GuiNodeId, bool> foldoutsByNode_ = [];

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

    public void SetSplitRatio(GuiNodeId nodeId, double ratio)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        if (double.IsNaN(ratio) || double.IsInfinity(ratio) || ratio <= 0d || ratio >= 1d)
        {
            throw new ArgumentOutOfRangeException(
                nameof(ratio),
                ratio,
                "Split ratio must be greater than 0 and less than 1.");
        }

        splitRatiosByNode_[nodeId] = ratio;
    }

    public bool TryGetSplitRatio(GuiNodeId nodeId, out double ratio)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return splitRatiosByNode_.TryGetValue(nodeId, out ratio);
    }

    public void SetToggle(GuiNodeId nodeId, bool isChecked)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        togglesByNode_[nodeId] = isChecked;
    }

    public bool TryGetToggle(GuiNodeId nodeId, out bool isChecked)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return togglesByNode_.TryGetValue(nodeId, out isChecked);
    }

    public void SetFoldoutExpanded(GuiNodeId nodeId, bool isExpanded)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        foldoutsByNode_[nodeId] = isExpanded;
    }

    public bool TryGetFoldoutExpanded(GuiNodeId nodeId, out bool isExpanded)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return foldoutsByNode_.TryGetValue(nodeId, out isExpanded);
    }

    public void ClearNodeState(GuiNodeId nodeId)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        textByNode_.Remove(nodeId);
        selectedItemsByNode_.Remove(nodeId);
        splitRatiosByNode_.Remove(nodeId);
        togglesByNode_.Remove(nodeId);
        foldoutsByNode_.Remove(nodeId);
    }
}
