using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Layout;
using Editor.Shell.Docking.DropTargets;
using Editor.Shell.ViewModels.Docking;

namespace Editor.Shell.Docking.Layout;

internal static class EditorDockLayoutGraph
{
    internal const string DynamicSplitIdPrefix = "split-user-";

    public static int GetNextDynamicSplitIndex(EditorDockNodeViewModel? node)
    {
        var nextIndex = 1;
        CollectNextDynamicSplitIndex(node, ref nextIndex);
        return nextIndex;
    }

    public static EditorDockNodeViewModel? InsertWindowNodeAtSplitter(
        EditorDockNodeViewModel? root,
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        EditorDockDropTarget target,
        Func<string> createSplitId)
    {
        if (!HasWeightedSplitLengths(targetSplit))
        {
            var replacement = CreateLocalSplitterInsertionSplit(
                targetSplit,
                insertedNode,
                target,
                createSplitId);
            return ReplaceNode(root, targetSplit, replacement, out _);
        }

        var entries = CreateSplitterInsertEntries(targetSplit, out var insertIndex);
        if (!TryInsertWeightedNode(entries, insertIndex, insertedNode))
        {
            return root;
        }

        var rebuilt = BuildWeightedSplit(
            targetSplit.Orientation,
            entries,
            0,
            entries.Count,
            createSplitId,
            out _);
        if (rebuilt is not EditorDockSplitNodeViewModel rebuiltSplit)
        {
            return root;
        }

        targetSplit.First = rebuiltSplit.First;
        targetSplit.Second = rebuiltSplit.Second;
        targetSplit.FirstLength = rebuiltSplit.FirstLength;
        targetSplit.SecondLength = rebuiltSplit.SecondLength;
        return root;
    }

    public static EditorDockNodeViewModel InsertWindowNodeAtWorkspaceEdge(
        EditorDockNodeViewModel? root,
        EditorDockDropOperation operation,
        EditorDockWindowNodeViewModel insertedNode,
        Func<string> createSplitId)
    {
        if (root is null)
        {
            return insertedNode;
        }

        return operation switch
        {
            EditorDockDropOperation.InsertWorkspaceLeft => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Horizontal,
                insertedNode,
                root,
                GetInsertedWorkspaceSideEdgeLength(),
                GetRetainedWorkspaceSideEdgeLength()),
            EditorDockDropOperation.InsertWorkspaceRight => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Horizontal,
                root,
                insertedNode,
                GetRetainedWorkspaceSideEdgeLength(),
                GetInsertedWorkspaceSideEdgeLength()),
            EditorDockDropOperation.InsertWorkspaceTop => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Vertical,
                insertedNode,
                root,
                GetInsertedEdgeLength(),
                GetRetainedEdgeLength()),
            EditorDockDropOperation.InsertWorkspaceBottom => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Vertical,
                root,
                insertedNode,
                GetRetainedEdgeLength(),
                GetInsertedEdgeLength()),
            _ => root,
        };
    }

    public static EditorDockSplitNodeViewModel CreateWindowInsertionSplit(
        EditorDockDropOperation operation,
        EditorDockWindowNodeViewModel targetNode,
        EditorDockWindowNodeViewModel insertedNode,
        Func<string> createSplitId)
    {
        return operation switch
        {
            EditorDockDropOperation.InsertLeft => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Horizontal,
                insertedNode,
                targetNode,
                GetInsertedWindowSplitLength(),
                GetRetainedWindowSplitLength()),
            EditorDockDropOperation.InsertRight => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Horizontal,
                targetNode,
                insertedNode,
                GetRetainedWindowSplitLength(),
                GetInsertedWindowSplitLength()),
            EditorDockDropOperation.InsertTop => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Vertical,
                insertedNode,
                targetNode,
                GetInsertedWindowSplitLength(),
                GetRetainedWindowSplitLength()),
            EditorDockDropOperation.InsertBottom => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Vertical,
                targetNode,
                insertedNode,
                GetRetainedWindowSplitLength(),
                GetInsertedWindowSplitLength()),
            _ => new EditorDockSplitNodeViewModel(
                createSplitId(),
                Orientation.Horizontal,
                targetNode,
                insertedNode,
                GetRetainedWindowSplitLength(),
                GetInsertedWindowSplitLength()),
        };
    }

    public static bool IsWindowInsertOperation(EditorDockDropOperation operation)
    {
        return operation is EditorDockDropOperation.InsertLeft
            or EditorDockDropOperation.InsertRight
            or EditorDockDropOperation.InsertTop
            or EditorDockDropOperation.InsertBottom;
    }

    public static bool IsWorkspaceEdgeInsertOperation(EditorDockDropOperation operation)
    {
        return operation is EditorDockDropOperation.InsertWorkspaceLeft
            or EditorDockDropOperation.InsertWorkspaceRight
            or EditorDockDropOperation.InsertWorkspaceTop
            or EditorDockDropOperation.InsertWorkspaceBottom;
    }

    public static EditorDockSplitNodeViewModel? FindSplitNode(
        EditorDockNodeViewModel? node,
        string splitId)
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

    public static bool TryFindWindowNode(
        EditorDockNodeViewModel? node,
        string windowId,
        EditorDockSplitNodeViewModel? parent,
        out EditorDockSplitNodeViewModel? parentSplit,
        out bool isFirstChild,
        out EditorDockWindowNodeViewModel? windowNode)
    {
        if (node is EditorDockWindowNodeViewModel window && window.Window.Id == windowId)
        {
            parentSplit = parent;
            isFirstChild = parent is not null && ReferenceEquals(parent.First, node);
            windowNode = window;
            return true;
        }

        if (node is EditorDockSplitNodeViewModel split)
        {
            if (TryFindWindowNode(split.First, windowId, split, out parentSplit, out isFirstChild, out windowNode))
            {
                return true;
            }

            if (TryFindWindowNode(split.Second, windowId, split, out parentSplit, out isFirstChild, out windowNode))
            {
                return true;
            }
        }

        parentSplit = null;
        isFirstChild = false;
        windowNode = null;
        return false;
    }

    public static bool IsSplitterInsertNoOp(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowViewModel sourceWindow)
    {
        if (sourceWindow.Tabs.Count != 1)
        {
            return false;
        }

        var entries = CreateSplitterInsertEntries(targetSplit, out var insertIndex);
        return insertIndex > 0
            && insertIndex < entries.Count
            && (IsWindowEntry(entries[insertIndex - 1], sourceWindow)
                || IsWindowEntry(entries[insertIndex], sourceWindow));
    }

    public static EditorDockNodeViewModel? ReplaceNode(
        EditorDockNodeViewModel? root,
        EditorDockNodeViewModel target,
        EditorDockNodeViewModel replacement,
        out bool replaced)
    {
        if (root is null)
        {
            replaced = false;
            return null;
        }

        if (ReferenceEquals(root, target))
        {
            replaced = true;
            return replacement;
        }

        replaced = ReplaceDescendant(root, target, replacement);
        return root;
    }

    public static EditorDockNodeViewModel? Normalize(
        EditorDockNodeViewModel? root,
        Func<string> createSplitId)
    {
        return root is null ? null : NormalizeNode(root, createSplitId);
    }

    private static void CollectNextDynamicSplitIndex(
        EditorDockNodeViewModel? node,
        ref int nextIndex)
    {
        if (node is not EditorDockSplitNodeViewModel split)
        {
            return;
        }

        if (split.Id.StartsWith(DynamicSplitIdPrefix, StringComparison.Ordinal))
        {
            var suffix = split.Id[DynamicSplitIdPrefix.Length..];
            if (int.TryParse(suffix, out var index) && index >= nextIndex)
            {
                nextIndex = index + 1;
            }
        }

        CollectNextDynamicSplitIndex(split.First, ref nextIndex);
        CollectNextDynamicSplitIndex(split.Second, ref nextIndex);
    }

    private static GridLength GetInsertedEdgeLength()
    {
        return new GridLength(1, GridUnitType.Star);
    }

    private static GridLength GetInsertedWorkspaceSideEdgeLength()
    {
        return new GridLength(1, GridUnitType.Star);
    }

    private static GridLength GetRetainedWorkspaceSideEdgeLength()
    {
        return new GridLength(4, GridUnitType.Star);
    }

    private static GridLength GetInsertedWindowSplitLength()
    {
        return new GridLength(1, GridUnitType.Star);
    }

    private static GridLength GetRetainedWindowSplitLength()
    {
        return new GridLength(1, GridUnitType.Star);
    }

    private static GridLength GetRetainedEdgeLength()
    {
        return new GridLength(2, GridUnitType.Star);
    }

    private static double GetSplitWeight(GridLength length)
    {
        if (!length.IsStar || double.IsNaN(length.Value) || double.IsInfinity(length.Value) || length.Value <= 0)
        {
            return 1d;
        }

        return Math.Clamp(length.Value, 0.05d, 16d);
    }

    private static bool HasWeightedSplitLengths(EditorDockSplitNodeViewModel split)
    {
        return HasWeightedSplitLength(split.FirstLength)
            && HasWeightedSplitLength(split.SecondLength);
    }

    private static bool HasWeightedSplitLength(GridLength length)
    {
        return length.IsStar
            && !double.IsNaN(length.Value)
            && !double.IsInfinity(length.Value)
            && length.Value > 0;
    }

    private static EditorDockSplitNodeViewModel CreateLocalSplitterInsertionSplit(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        EditorDockDropTarget target,
        Func<string> createSplitId)
    {
        if (TryCreateSymmetricLocalSplitterInsertion(
                targetSplit,
                insertedNode,
                createSplitId,
                out var symmetricSplit))
        {
            return symmetricSplit;
        }

        if (TryCreateMeasuredLocalSplitterInsertion(
                targetSplit,
                insertedNode,
                target,
                createSplitId,
                out var measuredSplit))
        {
            return measuredSplit;
        }

        return HasWeightedSplitLength(targetSplit.SecondLength) || !HasWeightedSplitLength(targetSplit.FirstLength)
            ? CreateTrailingLocalSplitterInsertion(targetSplit, insertedNode, createSplitId)
            : CreateLeadingLocalSplitterInsertion(targetSplit, insertedNode, createSplitId);
    }

    private static bool TryCreateMeasuredLocalSplitterInsertion(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        EditorDockDropTarget target,
        Func<string> createSplitId,
        out EditorDockSplitNodeViewModel replacement)
    {
        replacement = null!;
        if (target.SplitterFirstExtent is not { } firstExtent
            || target.SplitterSecondExtent is not { } secondExtent
            || firstExtent <= 0
            || secondExtent <= 0)
        {
            return false;
        }

        var retainedFirstLength = new GridLength(firstExtent / 2d, GridUnitType.Star);
        var insertedLength = new GridLength((firstExtent + secondExtent) / 2d, GridUnitType.Star);
        var retainedSecondLength = new GridLength(secondExtent / 2d, GridUnitType.Star);
        var trailingGroupLength = AddSplitLengths(insertedLength, retainedSecondLength);
        var trailingGroup = new EditorDockSplitNodeViewModel(
            createSplitId(),
            targetSplit.Orientation,
            insertedNode,
            targetSplit.Second,
            insertedLength,
            retainedSecondLength);

        replacement = new EditorDockSplitNodeViewModel(
            targetSplit.Id,
            targetSplit.Orientation,
            targetSplit.First,
            trailingGroup,
            retainedFirstLength,
            trailingGroupLength);
        return true;
    }

    private static bool TryCreateSymmetricLocalSplitterInsertion(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        Func<string> createSplitId,
        out EditorDockSplitNodeViewModel replacement)
    {
        replacement = null!;
        if (!CanScaleSplitLength(targetSplit.FirstLength)
            || !CanScaleSplitLength(targetSplit.SecondLength)
            || targetSplit.FirstLength.GridUnitType != targetSplit.SecondLength.GridUnitType)
        {
            return false;
        }

        var retainedFirstLength = ScaleSplitLength(targetSplit.FirstLength, 0.5d);
        var insertedFromFirstLength = ScaleSplitLength(targetSplit.FirstLength, 0.5d);
        var insertedFromSecondLength = ScaleSplitLength(targetSplit.SecondLength, 0.5d);
        var retainedSecondLength = ScaleSplitLength(targetSplit.SecondLength, 0.5d);
        var insertedLength = AddSplitLengths(insertedFromFirstLength, insertedFromSecondLength);
        var trailingGroupLength = AddSplitLengths(insertedLength, retainedSecondLength);
        var trailingGroup = new EditorDockSplitNodeViewModel(
            createSplitId(),
            targetSplit.Orientation,
            insertedNode,
            targetSplit.Second,
            insertedLength,
            retainedSecondLength);

        replacement = new EditorDockSplitNodeViewModel(
            targetSplit.Id,
            targetSplit.Orientation,
            targetSplit.First,
            trailingGroup,
            retainedFirstLength,
            trailingGroupLength);
        return true;
    }

    private static EditorDockSplitNodeViewModel CreateTrailingLocalSplitterInsertion(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        Func<string> createSplitId)
    {
        var trailingGroup = new EditorDockSplitNodeViewModel(
            createSplitId(),
            targetSplit.Orientation,
            insertedNode,
            targetSplit.Second,
            new GridLength(1, GridUnitType.Star),
            new GridLength(1, GridUnitType.Star));

        return new EditorDockSplitNodeViewModel(
            targetSplit.Id,
            targetSplit.Orientation,
            targetSplit.First,
            trailingGroup,
            targetSplit.FirstLength,
            targetSplit.SecondLength);
    }

    private static EditorDockSplitNodeViewModel CreateLeadingLocalSplitterInsertion(
        EditorDockSplitNodeViewModel targetSplit,
        EditorDockWindowNodeViewModel insertedNode,
        Func<string> createSplitId)
    {
        var leadingGroup = new EditorDockSplitNodeViewModel(
            createSplitId(),
            targetSplit.Orientation,
            targetSplit.First,
            insertedNode,
            new GridLength(1, GridUnitType.Star),
            new GridLength(1, GridUnitType.Star));

        return new EditorDockSplitNodeViewModel(
            targetSplit.Id,
            targetSplit.Orientation,
            leadingGroup,
            targetSplit.Second,
            targetSplit.FirstLength,
            targetSplit.SecondLength);
    }

    private static bool CanScaleSplitLength(GridLength length)
    {
        return !double.IsNaN(length.Value)
            && !double.IsInfinity(length.Value)
            && length.Value > 0
            && length.GridUnitType is GridUnitType.Star or GridUnitType.Pixel;
    }

    private static GridLength ScaleSplitLength(GridLength length, double factor)
    {
        var scaledValue = length.Value * factor;
        var minValue = length.GridUnitType == GridUnitType.Star ? 0.05d : 1d;
        return new GridLength(Math.Max(minValue, scaledValue), length.GridUnitType);
    }

    private static GridLength AddSplitLengths(GridLength first, GridLength second)
    {
        return new GridLength(first.Value + second.Value, first.GridUnitType);
    }

    private static List<WeightedDockNode> CreateSplitterInsertEntries(
        EditorDockSplitNodeViewModel targetSplit,
        out int insertIndex)
    {
        var entries = new List<WeightedDockNode>();
        var firstWeight = GetSplitWeight(targetSplit.FirstLength);
        var secondWeight = GetSplitWeight(targetSplit.SecondLength);
        CollectWeightedSplitChildren(targetSplit.First, targetSplit.Orientation, firstWeight, entries);
        insertIndex = entries.Count;
        CollectWeightedSplitChildren(targetSplit.Second, targetSplit.Orientation, secondWeight, entries);
        return entries;
    }

    private static bool TryInsertWeightedNode(
        List<WeightedDockNode> entries,
        int insertIndex,
        EditorDockWindowNodeViewModel insertedNode)
    {
        if (insertIndex <= 0 || insertIndex >= entries.Count)
        {
            return false;
        }

        var left = entries[insertIndex - 1];
        var right = entries[insertIndex];
        entries[insertIndex - 1] = left with { Weight = left.Weight * 0.5d };
        entries.Insert(insertIndex, new WeightedDockNode(insertedNode, (left.Weight + right.Weight) * 0.5d));
        entries[insertIndex + 1] = right with { Weight = right.Weight * 0.5d };
        return true;
    }

    private static bool IsWindowEntry(
        WeightedDockNode entry,
        EditorDockWindowViewModel window)
    {
        return entry.Node is EditorDockWindowNodeViewModel windowNode
            && ReferenceEquals(windowNode.Window, window);
    }

    private static void CollectWeightedSplitChildren(
        EditorDockNodeViewModel node,
        Orientation orientation,
        double weight,
        List<WeightedDockNode> children)
    {
        if (node is not EditorDockSplitNodeViewModel split
            || split.Orientation != orientation
            || !HasWeightedSplitLengths(split))
        {
            children.Add(new WeightedDockNode(node, weight));
            return;
        }

        var firstWeight = GetSplitWeight(split.FirstLength);
        var secondWeight = GetSplitWeight(split.SecondLength);
        var totalWeight = firstWeight + secondWeight;
        CollectWeightedSplitChildren(split.First, orientation, weight * firstWeight / totalWeight, children);
        CollectWeightedSplitChildren(split.Second, orientation, weight * secondWeight / totalWeight, children);
    }

    private static EditorDockNodeViewModel BuildWeightedSplit(
        Orientation orientation,
        IReadOnlyList<WeightedDockNode> children,
        int start,
        int count,
        Func<string> createSplitId,
        out double weight)
    {
        if (count == 1)
        {
            weight = children[start].Weight;
            return children[start].Node;
        }

        var splitCount = GetWeightedSplitCount(children, start, count);
        var first = BuildWeightedSplit(
            orientation,
            children,
            start,
            splitCount,
            createSplitId,
            out var firstWeight);
        var second = BuildWeightedSplit(
            orientation,
            children,
            start + splitCount,
            count - splitCount,
            createSplitId,
            out var secondWeight);
        weight = firstWeight + secondWeight;
        return new EditorDockSplitNodeViewModel(
            createSplitId(),
            orientation,
            first,
            second,
            new GridLength(firstWeight, GridUnitType.Star),
            new GridLength(secondWeight, GridUnitType.Star));
    }

    private static int GetWeightedSplitCount(
        IReadOnlyList<WeightedDockNode> children,
        int start,
        int count)
    {
        var totalWeight = 0d;
        for (var index = start; index < start + count; index++)
        {
            totalWeight += children[index].Weight;
        }

        var bestCount = 1;
        var bestDistance = double.PositiveInfinity;
        var runningWeight = 0d;
        for (var splitCount = 1; splitCount < count; splitCount++)
        {
            runningWeight += children[start + splitCount - 1].Weight;
            var distance = Math.Abs((totalWeight / 2d) - runningWeight);
            if (distance >= bestDistance)
            {
                continue;
            }

            bestDistance = distance;
            bestCount = splitCount;
        }

        return bestCount;
    }

    private static bool ReplaceDescendant(
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

        return ReplaceDescendant(split.First, target, replacement)
            || ReplaceDescendant(split.Second, target, replacement);
    }

    private static EditorDockNodeViewModel NormalizeNode(
        EditorDockNodeViewModel node,
        Func<string> createSplitId)
    {
        if (node is not EditorDockSplitNodeViewModel split)
        {
            return node;
        }

        split.First = NormalizeNode(split.First, createSplitId);
        split.Second = NormalizeNode(split.Second, createSplitId);

        if (!IsUserSplit(split))
        {
            return split;
        }

        var children = new List<WeightedDockNode>();
        CollectWeightedUserSplitChildren(split, split.Orientation, 1d, children);

        if (children.Count == 0)
        {
            return split;
        }

        if (children.Count == 1)
        {
            return children[0].Node;
        }

        return BuildWeightedSplit(
            split.Orientation,
            children,
            0,
            children.Count,
            createSplitId,
            out _);
    }

    private static void CollectWeightedUserSplitChildren(
        EditorDockNodeViewModel node,
        Orientation orientation,
        double weight,
        List<WeightedDockNode> children)
    {
        if (node is EditorDockSplitNodeViewModel split
            && split.Orientation == orientation
            && IsUserSplit(split)
            && HasWeightedSplitLengths(split))
        {
            var firstWeight = GetSplitWeight(split.FirstLength);
            var secondWeight = GetSplitWeight(split.SecondLength);
            var totalWeight = firstWeight + secondWeight;
            CollectWeightedUserSplitChildren(split.First, orientation, weight * firstWeight / totalWeight, children);
            CollectWeightedUserSplitChildren(split.Second, orientation, weight * secondWeight / totalWeight, children);
            return;
        }

        children.Add(new WeightedDockNode(node, weight));
    }

    private static bool IsUserSplit(EditorDockSplitNodeViewModel split)
    {
        return split.Id.StartsWith(DynamicSplitIdPrefix, StringComparison.Ordinal);
    }

    private readonly record struct WeightedDockNode(
        EditorDockNodeViewModel Node,
        double Weight);
}
