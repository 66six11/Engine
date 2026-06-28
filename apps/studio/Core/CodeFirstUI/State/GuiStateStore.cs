using System;
using System.Collections.Generic;

namespace Editor.Core.CodeFirstUI;

public sealed class GuiStateStore
{
    private readonly Dictionary<GuiNodeId, string> selectedItemsByNode_ = [];
    private readonly Dictionary<GuiNodeId, double> splitRatiosByNode_ = [];
    private readonly Dictionary<GuiNodeId, double> numericValuesByNode_ = [];
    private readonly Dictionary<GuiNodeId, GuiColorValue> colorValuesByNode_ = [];
    private readonly Dictionary<GuiNodeId, GuiVector3Value> vector3ValuesByNode_ = [];
    private readonly Dictionary<GuiNodeId, GuiVector2Value> vector2ValuesByNode_ = [];
    private readonly Dictionary<GuiNodeId, string> textByNode_ = [];
    private readonly Dictionary<GuiNodeId, bool> togglesByNode_ = [];
    private readonly Dictionary<GuiNodeId, bool> foldoutsByNode_ = [];
    private readonly Dictionary<GuiNodeId, Dictionary<string, bool>> navigationRouteExpansionByNode_ = [];

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

    public void SetSelectedRoute(GuiNodeId nodeId, string route)
    {
        SetSelectedItem(nodeId, route);
    }

    public bool TryGetSelectedRoute(GuiNodeId nodeId, out string? route)
    {
        return TryGetSelectedItem(nodeId, out route);
    }

    public void SetNavigationRouteExpanded(
        GuiNodeId nodeId,
        string route,
        bool isExpanded)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        if (string.IsNullOrWhiteSpace(route))
        {
            throw new ArgumentException("Navigation route must not be empty.", nameof(route));
        }

        if (!navigationRouteExpansionByNode_.TryGetValue(nodeId, out var routes))
        {
            routes = new Dictionary<string, bool>(StringComparer.Ordinal);
            navigationRouteExpansionByNode_[nodeId] = routes;
        }

        routes[route] = isExpanded;
    }

    public bool TryGetNavigationRouteExpanded(
        GuiNodeId nodeId,
        string route,
        out bool isExpanded)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        if (string.IsNullOrWhiteSpace(route))
        {
            isExpanded = false;
            return false;
        }

        if (navigationRouteExpansionByNode_.TryGetValue(nodeId, out var routes)
            && routes.TryGetValue(route, out isExpanded))
        {
            return true;
        }

        isExpanded = false;
        return false;
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

    public void SetNumericValue(GuiNodeId nodeId, double value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);
        if (double.IsNaN(value) || double.IsInfinity(value))
        {
            throw new ArgumentOutOfRangeException(
                nameof(value),
                value,
                "Numeric value must be finite.");
        }

        numericValuesByNode_[nodeId] = value;
    }

    public bool TryGetNumericValue(GuiNodeId nodeId, out double value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return numericValuesByNode_.TryGetValue(nodeId, out value);
    }

    public void SetColorValue(GuiNodeId nodeId, GuiColorValue value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        colorValuesByNode_[nodeId] = value;
    }

    public bool TryGetColorValue(GuiNodeId nodeId, out GuiColorValue value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return colorValuesByNode_.TryGetValue(nodeId, out value);
    }

    public void SetVector3Value(GuiNodeId nodeId, GuiVector3Value value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        vector3ValuesByNode_[nodeId] = value;
    }

    public bool TryGetVector3Value(GuiNodeId nodeId, out GuiVector3Value value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return vector3ValuesByNode_.TryGetValue(nodeId, out value);
    }

    public void SetVector2Value(GuiNodeId nodeId, GuiVector2Value value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        vector2ValuesByNode_[nodeId] = value;
    }

    public bool TryGetVector2Value(GuiNodeId nodeId, out GuiVector2Value value)
    {
        ArgumentNullException.ThrowIfNull(nodeId);

        return vector2ValuesByNode_.TryGetValue(nodeId, out value);
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
        numericValuesByNode_.Remove(nodeId);
        colorValuesByNode_.Remove(nodeId);
        vector3ValuesByNode_.Remove(nodeId);
        vector2ValuesByNode_.Remove(nodeId);
        togglesByNode_.Remove(nodeId);
        foldoutsByNode_.Remove(nodeId);
        navigationRouteExpansionByNode_.Remove(nodeId);
    }
}
