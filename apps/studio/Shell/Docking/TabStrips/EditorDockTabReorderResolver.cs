using System.Collections.Generic;
using Avalonia;

namespace Editor.Shell.Docking.TabStrips;

internal static class EditorDockTabReorderResolver
{
    public const double DefaultSwitchHysteresis = 5.0;

    public static int ResolveTargetIndex(
        double draggedTabCenterX,
        int sourceIndex,
        int currentTargetIndex,
        int tabCount,
        double draggedTabWidth,
        IReadOnlyList<Entry> entries,
        double switchHysteresis = DefaultSwitchHysteresis)
    {
        if (sourceIndex < 0)
        {
            return currentTargetIndex >= 0 ? currentTargetIndex : 0;
        }

        if (entries.Count == 0)
        {
            return sourceIndex;
        }

        var targetIndex = currentTargetIndex >= 0 ? currentTargetIndex : sourceIndex;
        while (TryResolveAdjacentTargetIndex(
                   draggedTabCenterX,
                   sourceIndex,
                   targetIndex,
                   tabCount,
                   ResolvePlaceholderWidth(sourceIndex, draggedTabWidth, entries),
                   entries,
                   switchHysteresis,
                   out var nextTargetIndex)
               && nextTargetIndex != targetIndex)
        {
            targetIndex = nextTargetIndex;
        }

        return targetIndex;
    }

    public static int ResolveExternalTargetIndex(
        double draggedTabCenterX,
        int currentTargetIndex,
        int tabCount,
        double draggedTabWidth,
        IReadOnlyList<Entry> entries,
        double switchHysteresis = DefaultSwitchHysteresis)
    {
        if (entries.Count == 0 || tabCount <= 0)
        {
            return 0;
        }

        var targetIndex = currentTargetIndex >= 0 ? currentTargetIndex : 0;
        while (TryResolveAdjacentTargetIndex(
                   draggedTabCenterX,
                   sourceIndex: null,
                   targetIndex,
                   tabCount,
                   ResolvePlaceholderWidth(sourceIndex: null, draggedTabWidth, entries),
                   entries,
                   switchHysteresis,
                   out var nextTargetIndex)
               && nextTargetIndex != targetIndex)
        {
            targetIndex = nextTargetIndex;
        }

        return targetIndex;
    }

    private static bool TryResolveAdjacentTargetIndex(
        double draggedTabCenterX,
        int? sourceIndex,
        int targetIndex,
        int tabCount,
        double placeholderWidth,
        IReadOnlyList<Entry> entries,
        double switchHysteresis,
        out int nextTargetIndex)
    {
        nextTargetIndex = targetIndex;
        var clampedTargetIndex = System.Math.Clamp(targetIndex, 0, tabCount);
        var currentX = entries[0].Bounds.X;
        var placeholderCenterX = 0d;
        var hasPlaceholder = false;
        var hasPrevious = false;
        var previousCenterX = 0d;
        var previousTabIndex = 0;
        var hasNext = false;
        var nextCenterX = 0d;
        var nextTabIndex = 0;

        for (var tabIndex = 0; tabIndex <= tabCount; tabIndex++)
        {
            if (clampedTargetIndex == tabIndex)
            {
                placeholderCenterX = currentX + (placeholderWidth / 2);
                hasPlaceholder = true;
                currentX += placeholderWidth;
            }

            if (tabIndex >= tabCount
                || !TryGetEntry(tabIndex, entries, out var entry)
                || entry.TabIndex == sourceIndex)
            {
                continue;
            }

            var entryWidth = entry.Bounds.Width > 0 ? entry.Bounds.Width : placeholderWidth;
            var entryCenterX = currentX + (entryWidth / 2);
            currentX += entryWidth;

            if (hasPlaceholder && !hasNext)
            {
                hasNext = true;
                nextCenterX = entryCenterX;
                nextTabIndex = entry.TabIndex;
                continue;
            }

            if (!hasPlaceholder)
            {
                hasPrevious = true;
                previousCenterX = entryCenterX;
                previousTabIndex = entry.TabIndex;
            }
        }

        if (!hasPlaceholder)
        {
            return false;
        }

        if (hasPrevious)
        {
            var leftBoundary = (previousCenterX + placeholderCenterX) / 2;
            if (draggedTabCenterX < leftBoundary - switchHysteresis)
            {
                nextTargetIndex = previousTabIndex;
                return true;
            }
        }

        if (hasNext)
        {
            var rightBoundary = (placeholderCenterX + nextCenterX) / 2;
            if (draggedTabCenterX >= rightBoundary + switchHysteresis)
            {
                nextTargetIndex = nextTabIndex + 1;
                return true;
            }
        }

        return false;
    }

    private static double ResolvePlaceholderWidth(
        int? sourceIndex,
        double draggedTabWidth,
        IReadOnlyList<Entry> entries)
    {
        if (sourceIndex is { } localSourceIndex
            && TryGetEntry(localSourceIndex, entries, out var sourceEntry)
            && sourceEntry.Bounds.Width > 0)
        {
            return sourceEntry.Bounds.Width;
        }

        return draggedTabWidth > 0 ? draggedTabWidth : 1d;
    }

    private static bool TryGetEntry(
        int tabIndex,
        IReadOnlyList<Entry> entries,
        out Entry entry)
    {
        if (tabIndex >= 0
            && tabIndex < entries.Count
            && entries[tabIndex].TabIndex == tabIndex)
        {
            entry = entries[tabIndex];
            return true;
        }

        foreach (var candidate in entries)
        {
            if (candidate.TabIndex == tabIndex)
            {
                entry = candidate;
                return true;
            }
        }

        entry = default;
        return false;
    }

    internal readonly record struct Entry(int TabIndex, Rect Bounds);
}
