using System.Collections.Generic;
using Avalonia;
using Avalonia.Layout;
using Editor.Shell.Docking.TabStrips;

namespace Editor.Shell.Docking.DropTargets;

internal static class EditorDockHitTestService
{
    private const double SplitterHitSlop = 8.0;
    private const double SplitterPreviewBreadth = 76.0;
    private const double WindowGuideCenterWidth = 132.0;
    private const double WindowGuideCenterHeight = 58.0;
    private const double WindowGuideHotspotWidth = 96.0;
    private const double WindowGuideHotspotHeight = 56.0;
    private const double WindowGuideInset = 12.0;
    private const double WindowInsertPreviewShare = 0.5;
    private const double WorkspaceSideEdgePreviewShare = 1.0 / 5.0;
    private const double WorkspaceTopBottomEdgePreviewShare = 1.0 / 3.0;
    private const double WorkspaceSideEdgeBandWidth = 6.0;
    private const double WorkspaceTopBottomEdgeBandHeight = 6.0;
    private const double TabInsertPlaceholderMinWidth = 96.0;
    private const double TabInsertPlaceholderMaxWidth = 180.0;
    private const double RejectPreviewWidth = 172.0;
    private const double RejectPreviewHeight = 48.0;
    private const double FloatPreviewWidth = 320.0;
    private const double FloatPreviewHeight = 220.0;
    private const double FloatPreviewTabPointerX = 66.0;
    private const double FloatPreviewTabPointerY = 21.0;

    public static EditorDockDropTarget HitTest(
        Point pointer,
        Rect workspaceBounds,
        IReadOnlyList<EditorDockWindowBounds> windows,
        IReadOnlyList<EditorDockSplitterBounds> splitters,
        bool allowOutOfBoundsWorkspaceEdge = false,
        double? tabInsertProbeX = null,
        string? tabInsertPreviewWindowId = null,
        int? tabInsertCurrentTargetIndex = null,
        double? tabInsertDraggedTabWidth = null)
    {
        if (windows.Count == 0)
        {
            return CreateFloatTarget(pointer);
        }

        if (!workspaceBounds.Contains(pointer))
        {
            if (allowOutOfBoundsWorkspaceEdge
                && TryCreateOutOfBoundsWorkspaceEdgeTarget(pointer, workspaceBounds, out var outOfBoundsTarget))
            {
                return outOfBoundsTarget;
            }

            return CreateFloatTarget(pointer);
        }

        foreach (var window in windows)
        {
            if (window.TabWellBounds.Contains(pointer))
            {
                return HitTestWindowGuide(
                    pointer,
                    window,
                    tabInsertProbeX,
                    tabInsertPreviewWindowId,
                    tabInsertCurrentTargetIndex,
                    tabInsertDraggedTabWidth);
            }
        }

        foreach (var splitter in splitters)
        {
            if (Inflate(splitter.Bounds, SplitterHitSlop, SplitterHitSlop).Contains(pointer))
            {
                return CreateSplitterTarget(splitter);
            }
        }

        if (TryCreateWorkspaceEdgeTarget(
                pointer,
                workspaceBounds,
                out var workspaceEdgeTarget))
        {
            return workspaceEdgeTarget;
        }

        foreach (var window in windows)
        {
            if (window.Bounds.Contains(pointer))
            {
                return HitTestWindowGuide(
                    pointer,
                    window,
                    tabInsertProbeX,
                    tabInsertPreviewWindowId,
                    tabInsertCurrentTargetIndex,
                    tabInsertDraggedTabWidth);
            }
        }

        return CreateFloatTarget(pointer);
    }

    private static bool TryCreateOutOfBoundsWorkspaceEdgeTarget(
        Point pointer,
        Rect workspaceBounds,
        out EditorDockDropTarget target)
    {
        target = null!;
        if (workspaceBounds.Width <= 0 || workspaceBounds.Height <= 0)
        {
            return false;
        }

        var leftDistance = pointer.X < workspaceBounds.X
            ? workspaceBounds.X - pointer.X
            : double.PositiveInfinity;
        var rightDistance = pointer.X > workspaceBounds.Right
            ? pointer.X - workspaceBounds.Right
            : double.PositiveInfinity;
        var topDistance = pointer.Y < workspaceBounds.Y
            ? workspaceBounds.Y - pointer.Y
            : double.PositiveInfinity;
        var bottomDistance = pointer.Y > workspaceBounds.Bottom
            ? pointer.Y - workspaceBounds.Bottom
            : double.PositiveInfinity;
        if (double.IsPositiveInfinity(leftDistance)
            && double.IsPositiveInfinity(rightDistance)
            && double.IsPositiveInfinity(topDistance)
            && double.IsPositiveInfinity(bottomDistance))
        {
            return false;
        }

        EditorDockDropOperation operation;
        if (leftDistance <= rightDistance
            && leftDistance <= topDistance
            && leftDistance <= bottomDistance)
        {
            operation = EditorDockDropOperation.InsertWorkspaceLeft;
        }
        else if (rightDistance <= topDistance
                 && rightDistance <= bottomDistance)
        {
            operation = EditorDockDropOperation.InsertWorkspaceRight;
        }
        else if (topDistance <= bottomDistance)
        {
            operation = EditorDockDropOperation.InsertWorkspaceTop;
        }
        else
        {
            operation = EditorDockDropOperation.InsertWorkspaceBottom;
        }

        target = CreateWorkspaceEdgeTarget(operation, workspaceBounds);
        return true;
    }

    private static EditorDockDropTarget HitTestWindowGuide(
        Point pointer,
        EditorDockWindowBounds window,
        double? tabInsertProbeX = null,
        string? tabInsertPreviewWindowId = null,
        int? tabInsertCurrentTargetIndex = null,
        double? tabInsertDraggedTabWidth = null)
    {
        if (window.TabWellBounds.Contains(pointer))
        {
            return window.TabBounds.Count > 0
                ? CreateTabInsertTarget(
                    window,
                    pointer,
                    tabInsertProbeX ?? pointer.X,
                    tabInsertPreviewWindowId,
                    tabInsertCurrentTargetIndex,
                    tabInsertDraggedTabWidth)
                : CreateTabTarget(window);
        }

        if (window.IsDragSource && window.TabCount <= 1)
        {
            return CreateFloatTarget(pointer);
        }

        if (!window.AllowsWindowInsertion)
        {
            return CreateFloatTarget(pointer);
        }

        var center = GetCenteredRect(window.Bounds.Center, WindowGuideCenterWidth, WindowGuideCenterHeight);
        var top = GetCenteredRect(
            new Point(
                window.Bounds.Center.X,
                window.Bounds.Y + WindowGuideInset + (WindowGuideHotspotHeight / 2)),
            WindowGuideHotspotWidth,
            WindowGuideHotspotHeight);
        var right = GetCenteredRect(
            new Point(
                window.Bounds.Right - WindowGuideInset - (WindowGuideHotspotWidth / 2),
                window.Bounds.Center.Y),
            WindowGuideHotspotWidth,
            WindowGuideHotspotHeight);
        var bottom = GetCenteredRect(
            new Point(
                window.Bounds.Center.X,
                window.Bounds.Bottom - WindowGuideInset - (WindowGuideHotspotHeight / 2)),
            WindowGuideHotspotWidth,
            WindowGuideHotspotHeight);
        var left = GetCenteredRect(
            new Point(
                window.Bounds.X + WindowGuideInset + (WindowGuideHotspotWidth / 2),
                window.Bounds.Center.Y),
            WindowGuideHotspotWidth,
            WindowGuideHotspotHeight);

        if (center.Contains(pointer))
        {
            return CreateFloatTarget(pointer);
        }

        if (top.Contains(pointer))
        {
            return CreateWindowInsertTarget(EditorDockDropOperation.InsertTop, window);
        }

        if (right.Contains(pointer))
        {
            return CreateWindowInsertTarget(EditorDockDropOperation.InsertRight, window);
        }

        if (bottom.Contains(pointer))
        {
            return CreateWindowInsertTarget(EditorDockDropOperation.InsertBottom, window);
        }

        if (left.Contains(pointer))
        {
            return CreateWindowInsertTarget(EditorDockDropOperation.InsertLeft, window);
        }

        return CreateFloatTarget(pointer);
    }

    private static EditorDockDropTarget CreateTabTarget(EditorDockWindowBounds window)
    {
        return new EditorDockDropTarget(
            EditorDockDropOperation.TabInto,
            EditorDockDropGuideKind.Merge,
            window.Area,
            window.WindowId,
            window.Bounds,
            $"Tab into {window.WindowId}");
    }

    private static EditorDockDropTarget CreateTabInsertTarget(
        EditorDockWindowBounds window,
        Point pointer,
        double probeX,
        string? tabInsertPreviewWindowId,
        int? tabInsertCurrentTargetIndex,
        double? tabInsertDraggedTabWidth)
    {
        var entries = CreateTabInsertEntries(window);
        var sourceTabIndex = GetDragSourceTabIndex(window);
        var currentTargetIndex = window.WindowId == tabInsertPreviewWindowId
            ? tabInsertCurrentTargetIndex
            : null;
        var placeholderWidth = GetTabInsertPlaceholderWidth(
            entries,
            currentTargetIndex ?? sourceTabIndex ?? 0,
            tabInsertDraggedTabWidth);
        var targetIndex = sourceTabIndex is null
            ? EditorDockTabReorderResolver.ResolveExternalTargetIndex(
                probeX,
                currentTargetIndex: currentTargetIndex ?? 0,
                window.TabCount,
                placeholderWidth,
                entries)
            : EditorDockTabReorderResolver.ResolveTargetIndex(
                probeX,
                sourceTabIndex.Value,
                currentTargetIndex ?? sourceTabIndex.Value,
                window.TabCount,
                placeholderWidth,
                entries);
        var lineX = ResolveTabInsertLineX(entries, targetIndex, window.TabWellBounds.X);

        if (sourceTabIndex is { } sourceIndex
            && (targetIndex == sourceIndex || targetIndex == sourceIndex + 1))
        {
            return CreateRejectTarget(pointer, "Source tab disabled while dragging");
        }

        placeholderWidth = GetTabInsertPlaceholderWidth(
            entries,
            targetIndex,
            tabInsertDraggedTabWidth);
        var placeholderX = Clamp(
            lineX - (placeholderWidth / 2),
            window.TabWellBounds.X,
            System.Math.Max(window.TabWellBounds.X, window.TabWellBounds.Right - placeholderWidth));
        var previewBounds = new Rect(
            placeholderX,
            window.TabWellBounds.Y,
            placeholderWidth,
            window.TabWellBounds.Height);
        return new EditorDockDropTarget(
            EditorDockDropOperation.InsertTabAtIndex,
            EditorDockDropGuideKind.Insert,
            window.Area,
            window.WindowId,
            previewBounds,
            $"Insert tab at {targetIndex + 1} in {window.WindowId}",
            targetIndex);
    }

    private static double ResolveTabInsertLineX(
        List<EditorDockTabReorderResolver.Entry> entries,
        int targetIndex,
        double fallbackX)
    {
        foreach (var entry in entries)
        {
            if (entry.TabIndex >= targetIndex)
            {
                return entry.Bounds.X;
            }
        }

        return entries.Count > 0 ? entries[^1].Bounds.Right : fallbackX;
    }

    private static List<EditorDockTabReorderResolver.Entry> CreateTabInsertEntries(EditorDockWindowBounds window)
    {
        var tabBounds = new List<EditorDockTabBounds>(window.TabBounds);
        tabBounds.Sort((left, right) => left.TabIndex.CompareTo(right.TabIndex));

        var entries = new List<EditorDockTabReorderResolver.Entry>(tabBounds.Count);
        var currentX = double.IsNaN(window.TabContentOriginX) || double.IsInfinity(window.TabContentOriginX)
            ? window.TabWellBounds.X
            : window.TabContentOriginX;
        foreach (var tab in tabBounds)
        {
            var width = tab.Bounds.Width > 0 ? tab.Bounds.Width : TabInsertPlaceholderMinWidth;
            var height = tab.Bounds.Height > 0 ? tab.Bounds.Height : window.TabWellBounds.Height;
            entries.Add(new EditorDockTabReorderResolver.Entry(
                tab.TabIndex,
                new Rect(currentX, tab.Bounds.Y, width, height)));
            currentX += width;
        }

        return entries;
    }

    private static int? GetDragSourceTabIndex(EditorDockWindowBounds window)
    {
        if (window.DragSourceTabIndex is { } sourceIndex)
        {
            return sourceIndex;
        }

        foreach (var tab in window.TabBounds)
        {
            if (tab.IsDragSource)
            {
                return tab.TabIndex;
            }
        }

        return null;
    }

    private static double GetTabInsertPlaceholderWidth(
        List<EditorDockTabReorderResolver.Entry> entries,
        int targetIndex,
        double? draggedTabWidth)
    {
        if (draggedTabWidth is > 0)
        {
            return draggedTabWidth.Value;
        }

        if (entries.Count == 0)
        {
            return TabInsertPlaceholderMinWidth;
        }

        var nearestTabBounds = entries[0].Bounds;
        foreach (var entry in entries)
        {
            nearestTabBounds = entry.Bounds;
            if (entry.TabIndex >= targetIndex)
            {
                break;
            }
        }

        return Clamp(
            nearestTabBounds.Width,
            TabInsertPlaceholderMinWidth,
            TabInsertPlaceholderMaxWidth);
    }

    private static EditorDockDropTarget CreateSplitterTarget(EditorDockSplitterBounds splitter)
    {
        return new EditorDockDropTarget(
            EditorDockDropOperation.SplitBetween,
            EditorDockDropGuideKind.Insert,
            null,
            splitter.SplitterId,
            GetSplitterPreviewBounds(splitter),
            $"Split at {splitter.SplitterId}",
            SplitterFirstExtent: GetSplitterExtent(splitter.Orientation, splitter.FirstBounds),
            SplitterSecondExtent: GetSplitterExtent(splitter.Orientation, splitter.SecondBounds));
    }

    private static bool TryCreateWorkspaceEdgeTarget(
        Point pointer,
        Rect workspaceBounds,
        out EditorDockDropTarget target)
    {
        target = null!;
        if (workspaceBounds.Width <= 0 || workspaceBounds.Height <= 0)
        {
            return false;
        }

        var leftDistance = pointer.X - workspaceBounds.X;
        var rightDistance = workspaceBounds.Right - pointer.X;
        var topDistance = pointer.Y - workspaceBounds.Y;
        var bottomDistance = workspaceBounds.Bottom - pointer.Y;
        var horizontalDistance = System.Math.Min(leftDistance, rightDistance);
        var verticalDistance = System.Math.Min(topDistance, bottomDistance);
        if (horizontalDistance > WorkspaceSideEdgeBandWidth
            && verticalDistance > WorkspaceTopBottomEdgeBandHeight)
        {
            return false;
        }

        var operation = leftDistance <= rightDistance
            && leftDistance <= topDistance
            && leftDistance <= bottomDistance
                ? EditorDockDropOperation.InsertWorkspaceLeft
                : rightDistance <= topDistance
                  && rightDistance <= bottomDistance
                    ? EditorDockDropOperation.InsertWorkspaceRight
                    : topDistance <= bottomDistance
                        ? EditorDockDropOperation.InsertWorkspaceTop
                        : EditorDockDropOperation.InsertWorkspaceBottom;
        target = CreateWorkspaceEdgeTarget(operation, workspaceBounds);
        return true;
    }

    private static EditorDockDropTarget CreateWorkspaceEdgeTarget(
        EditorDockDropOperation operation,
        Rect workspaceBounds)
    {
        return new EditorDockDropTarget(
            operation,
            EditorDockDropGuideKind.Insert,
            null,
            null,
            GetWorkspaceEdgePreviewBounds(operation, workspaceBounds),
            GetWorkspaceEdgeLabel(operation));
    }

    private static EditorDockDropTarget CreateWindowInsertTarget(
        EditorDockDropOperation operation,
        EditorDockWindowBounds window)
    {
        return new EditorDockDropTarget(
            operation,
            EditorDockDropGuideKind.Insert,
            window.Area,
            window.WindowId,
            GetWindowInsertPreviewBounds(operation, window.Bounds),
            GetWindowInsertLabel(operation, window.WindowId));
    }

    private static EditorDockDropTarget CreateFloatTarget(Point pointer)
    {
        var previewBounds = new Rect(
            pointer.X - FloatPreviewTabPointerX,
            pointer.Y - FloatPreviewTabPointerY,
            FloatPreviewWidth,
            FloatPreviewHeight);

        return new EditorDockDropTarget(
            EditorDockDropOperation.Float,
            EditorDockDropGuideKind.Float,
            null,
            null,
            previewBounds,
            "Float window");
    }

    private static EditorDockDropTarget CreateRejectTarget(Point pointer, string label)
    {
        var previewBounds = new Rect(
            pointer.X - (RejectPreviewWidth / 2),
            pointer.Y - (RejectPreviewHeight / 2),
            RejectPreviewWidth,
            RejectPreviewHeight);

        return EditorDockDropTarget.Reject(previewBounds, label);
    }

    private static Rect GetSplitterPreviewBounds(EditorDockSplitterBounds splitter)
    {
        if (TryGetSharedSplitterPreviewBounds(splitter, out var previewBounds))
        {
            return previewBounds;
        }

        if (splitter.Orientation == Orientation.Horizontal)
        {
            return new Rect(
                splitter.Bounds.Center.X - (SplitterPreviewBreadth / 2),
                splitter.Bounds.Y,
                SplitterPreviewBreadth,
                splitter.Bounds.Height);
        }

        return new Rect(
            splitter.Bounds.X,
            splitter.Bounds.Center.Y - (SplitterPreviewBreadth / 2),
            splitter.Bounds.Width,
            SplitterPreviewBreadth);
    }

    private static bool TryGetSharedSplitterPreviewBounds(
        EditorDockSplitterBounds splitter,
        out Rect previewBounds)
    {
        previewBounds = default;
        var first = splitter.FirstBounds;
        var second = splitter.SecondBounds;
        if (!IsUsableBounds(first) || !IsUsableBounds(second))
        {
            return false;
        }

        if (splitter.Orientation == Orientation.Horizontal)
        {
            var x = first.Center.X;
            var right = second.Center.X;
            var y = System.Math.Max(first.Y, second.Y);
            var bottom = System.Math.Min(first.Bottom, second.Bottom);
            if (bottom <= y)
            {
                y = System.Math.Min(first.Y, second.Y);
                bottom = System.Math.Max(first.Bottom, second.Bottom);
            }

            previewBounds = new Rect(x, y, right - x, bottom - y);
            return IsUsableBounds(previewBounds);
        }

        var verticalY = first.Center.Y;
        var verticalBottom = second.Center.Y;
        var verticalX = System.Math.Max(first.X, second.X);
        var verticalRight = System.Math.Min(first.Right, second.Right);
        if (verticalRight <= verticalX)
        {
            verticalX = System.Math.Min(first.X, second.X);
            verticalRight = System.Math.Max(first.Right, second.Right);
        }

        previewBounds = new Rect(
            verticalX,
            verticalY,
            verticalRight - verticalX,
            verticalBottom - verticalY);
        return IsUsableBounds(previewBounds);
    }

    private static Rect GetWindowInsertPreviewBounds(EditorDockDropOperation operation, Rect windowBounds)
    {
        var previewWidth = windowBounds.Width * WindowInsertPreviewShare;
        var previewHeight = windowBounds.Height * WindowInsertPreviewShare;
        return operation switch
        {
            EditorDockDropOperation.InsertLeft => new Rect(
                windowBounds.X,
                windowBounds.Y,
                previewWidth,
                windowBounds.Height),
            EditorDockDropOperation.InsertRight => new Rect(
                windowBounds.Right - previewWidth,
                windowBounds.Y,
                previewWidth,
                windowBounds.Height),
            EditorDockDropOperation.InsertTop => new Rect(
                windowBounds.X,
                windowBounds.Y,
                windowBounds.Width,
                previewHeight),
            EditorDockDropOperation.InsertBottom => new Rect(
                windowBounds.X,
                windowBounds.Bottom - previewHeight,
                windowBounds.Width,
                previewHeight),
            _ => windowBounds,
        };
    }

    private static Rect GetWorkspaceEdgePreviewBounds(EditorDockDropOperation operation, Rect workspaceBounds)
    {
        var previewWidth = workspaceBounds.Width * WorkspaceSideEdgePreviewShare;
        var previewHeight = workspaceBounds.Height * WorkspaceTopBottomEdgePreviewShare;
        return operation switch
        {
            EditorDockDropOperation.InsertWorkspaceLeft => new Rect(
                workspaceBounds.X,
                workspaceBounds.Y,
                previewWidth,
                workspaceBounds.Height),
            EditorDockDropOperation.InsertWorkspaceRight => new Rect(
                workspaceBounds.Right - previewWidth,
                workspaceBounds.Y,
                previewWidth,
                workspaceBounds.Height),
            EditorDockDropOperation.InsertWorkspaceTop => new Rect(
                workspaceBounds.X,
                workspaceBounds.Y,
                workspaceBounds.Width,
                previewHeight),
            EditorDockDropOperation.InsertWorkspaceBottom => new Rect(
                workspaceBounds.X,
                workspaceBounds.Bottom - previewHeight,
                workspaceBounds.Width,
                previewHeight),
            _ => workspaceBounds,
        };
    }

    private static string GetWorkspaceEdgeLabel(EditorDockDropOperation operation)
    {
        return operation switch
        {
            EditorDockDropOperation.InsertWorkspaceLeft => "Insert at workspace left edge",
            EditorDockDropOperation.InsertWorkspaceRight => "Insert at workspace right edge",
            EditorDockDropOperation.InsertWorkspaceTop => "Insert at workspace top edge",
            EditorDockDropOperation.InsertWorkspaceBottom => "Insert at workspace bottom edge",
            _ => "Insert at workspace edge",
        };
    }

    private static string GetWindowInsertLabel(EditorDockDropOperation operation, string windowId)
    {
        return operation switch
        {
            EditorDockDropOperation.InsertLeft => $"Insert left of {windowId}",
            EditorDockDropOperation.InsertRight => $"Insert right of {windowId}",
            EditorDockDropOperation.InsertTop => $"Insert above {windowId}",
            EditorDockDropOperation.InsertBottom => $"Insert below {windowId}",
            _ => $"Insert near {windowId}",
        };
    }

    private static Rect GetCenteredRect(Point center, double width, double height)
    {
        return new Rect(
            center.X - (width / 2),
            center.Y - (height / 2),
            width,
            height);
    }

    private static Rect Inflate(Rect rect, double x, double y)
    {
        return new Rect(
            rect.X - x,
            rect.Y - y,
            rect.Width + (x * 2),
            rect.Height + (y * 2));
    }

    private static double? GetSplitterExtent(Orientation orientation, Rect bounds)
    {
        var extent = orientation == Orientation.Horizontal
            ? bounds.Width
            : bounds.Height;
        return extent > 0 ? extent : null;
    }

    private static bool IsUsableBounds(Rect bounds)
    {
        return bounds.Width > 0 && bounds.Height > 0;
    }

    private static double Clamp(double value, double min, double max)
    {
        return System.Math.Min(System.Math.Max(value, min), max);
    }
}
