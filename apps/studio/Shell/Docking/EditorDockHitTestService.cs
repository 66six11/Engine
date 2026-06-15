using System.Collections.Generic;
using Avalonia;
using Avalonia.Layout;

namespace Editor.Shell.Docking;

public sealed class EditorDockHitTestService
{
    private const double SplitterHitSlop = 8.0;
    private const double SplitterPreviewBreadth = 76.0;
    private const double WindowGuideCenterWidth = 132.0;
    private const double WindowGuideCenterHeight = 58.0;
    private const double WindowGuideHotspotWidth = 72.0;
    private const double WindowGuideHotspotHeight = 44.0;
    private const double WindowGuideInset = 12.0;
    private const double WindowInsertPreviewBreadth = 112.0;
    private const double WorkspaceEdgeBandWidth = 40.0;
    private const double WorkspaceEdgeBandHeight = 36.0;
    private const double WorkspaceEdgePreviewBreadth = 112.0;
    private const double TabInsertPlaceholderMinWidth = 132.0;
    private const double TabInsertPlaceholderMaxWidth = 240.0;
    private const double RejectPreviewWidth = 172.0;
    private const double RejectPreviewHeight = 48.0;
    private const double FloatPreviewWidth = 260.0;
    private const double FloatPreviewHeight = 180.0;

    public EditorDockDropTarget HitTest(
        Point pointer,
        Rect workspaceBounds,
        IReadOnlyList<EditorDockWindowBounds> windows,
        IReadOnlyList<EditorDockSplitterBounds> splitters)
    {
        if (windows.Count == 0)
        {
            return CreateFloatTarget(pointer);
        }

        if (!workspaceBounds.Contains(pointer))
        {
            return CreateFloatTarget(pointer);
        }

        foreach (var window in windows)
        {
            if (window.TabWellBounds.Contains(pointer))
            {
                return HitTestWindowGuide(pointer, window);
            }
        }

        if (TryCreateWorkspaceEdgeTarget(pointer, workspaceBounds, out var workspaceEdgeTarget))
        {
            return workspaceEdgeTarget;
        }

        foreach (var splitter in splitters)
        {
            if (Inflate(splitter.Bounds, SplitterHitSlop, SplitterHitSlop).Contains(pointer))
            {
                return CreateSplitterTarget(splitter);
            }
        }

        foreach (var window in windows)
        {
            if (window.Bounds.Contains(pointer))
            {
                return HitTestWindowGuide(pointer, window);
            }
        }

        return CreateFloatTarget(pointer);
    }

    private static EditorDockDropTarget HitTestWindowGuide(Point pointer, EditorDockWindowBounds window)
    {
        if (window.TabWellBounds.Contains(pointer))
        {
            return window.TabBounds.Count > 0
                ? CreateTabInsertTarget(window, pointer)
                : CreateTabTarget(window);
        }

        if (window.IsDragSource)
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
            window.TabWellBounds,
            $"Tab into {window.WindowId}");
    }

    private static EditorDockDropTarget CreateTabInsertTarget(EditorDockWindowBounds window, Point pointer)
    {
        var targetIndex = window.TabBounds.Count;
        var lineX = window.TabBounds[^1].Bounds.Right;
        var sourceTabIndex = GetDragSourceTabIndex(window);
        foreach (var tab in window.TabBounds)
        {
            if (pointer.X >= tab.Bounds.Center.X)
            {
                continue;
            }

            targetIndex = tab.TabIndex;
            lineX = tab.Bounds.X;
            break;
        }

        if (sourceTabIndex is { } sourceIndex
            && (targetIndex == sourceIndex || targetIndex == sourceIndex + 1))
        {
            return CreateRejectTarget(pointer, "Source tab disabled while dragging");
        }

        var placeholderWidth = GetTabInsertPlaceholderWidth(window, targetIndex);
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

    private static int? GetDragSourceTabIndex(EditorDockWindowBounds window)
    {
        foreach (var tab in window.TabBounds)
        {
            if (tab.IsDragSource)
            {
                return tab.TabIndex;
            }
        }

        return null;
    }

    private static double GetTabInsertPlaceholderWidth(EditorDockWindowBounds window, int targetIndex)
    {
        if (window.TabBounds.Count == 0)
        {
            return TabInsertPlaceholderMinWidth;
        }

        var nearestTabIndex = System.Math.Clamp(targetIndex, 0, window.TabBounds.Count - 1);
        return Clamp(
            window.TabBounds[nearestTabIndex].Bounds.Width,
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
            $"Split at {splitter.SplitterId}");
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
        var isHorizontalEdge = horizontalDistance <= WorkspaceEdgeBandWidth;
        var isVerticalEdge = verticalDistance <= WorkspaceEdgeBandHeight;
        if (!isHorizontalEdge && !isVerticalEdge)
        {
            return false;
        }

        var operation = isHorizontalEdge && (!isVerticalEdge || horizontalDistance <= verticalDistance)
            ? leftDistance <= rightDistance
                ? EditorDockDropOperation.InsertWorkspaceLeft
                : EditorDockDropOperation.InsertWorkspaceRight
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
            pointer.X - (FloatPreviewWidth / 2),
            pointer.Y - 24,
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

    private static Rect GetWindowInsertPreviewBounds(EditorDockDropOperation operation, Rect windowBounds)
    {
        return operation switch
        {
            EditorDockDropOperation.InsertLeft => new Rect(
                windowBounds.X,
                windowBounds.Y,
                System.Math.Min(WindowInsertPreviewBreadth, windowBounds.Width),
                windowBounds.Height),
            EditorDockDropOperation.InsertRight => new Rect(
                windowBounds.Right - System.Math.Min(WindowInsertPreviewBreadth, windowBounds.Width),
                windowBounds.Y,
                System.Math.Min(WindowInsertPreviewBreadth, windowBounds.Width),
                windowBounds.Height),
            EditorDockDropOperation.InsertTop => new Rect(
                windowBounds.X,
                windowBounds.Y,
                windowBounds.Width,
                System.Math.Min(WindowInsertPreviewBreadth, windowBounds.Height)),
            EditorDockDropOperation.InsertBottom => new Rect(
                windowBounds.X,
                windowBounds.Bottom - System.Math.Min(WindowInsertPreviewBreadth, windowBounds.Height),
                windowBounds.Width,
                System.Math.Min(WindowInsertPreviewBreadth, windowBounds.Height)),
            _ => windowBounds,
        };
    }

    private static Rect GetWorkspaceEdgePreviewBounds(EditorDockDropOperation operation, Rect workspaceBounds)
    {
        return operation switch
        {
            EditorDockDropOperation.InsertWorkspaceLeft => new Rect(
                workspaceBounds.X,
                workspaceBounds.Y,
                System.Math.Min(WorkspaceEdgePreviewBreadth, workspaceBounds.Width),
                workspaceBounds.Height),
            EditorDockDropOperation.InsertWorkspaceRight => new Rect(
                workspaceBounds.Right - System.Math.Min(WorkspaceEdgePreviewBreadth, workspaceBounds.Width),
                workspaceBounds.Y,
                System.Math.Min(WorkspaceEdgePreviewBreadth, workspaceBounds.Width),
                workspaceBounds.Height),
            EditorDockDropOperation.InsertWorkspaceTop => new Rect(
                workspaceBounds.X,
                workspaceBounds.Y,
                workspaceBounds.Width,
                System.Math.Min(WorkspaceEdgePreviewBreadth, workspaceBounds.Height)),
            EditorDockDropOperation.InsertWorkspaceBottom => new Rect(
                workspaceBounds.X,
                workspaceBounds.Bottom - System.Math.Min(WorkspaceEdgePreviewBreadth, workspaceBounds.Height),
                workspaceBounds.Width,
                System.Math.Min(WorkspaceEdgePreviewBreadth, workspaceBounds.Height)),
            _ => workspaceBounds,
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

    private static double Clamp(double value, double min, double max)
    {
        return System.Math.Min(System.Math.Max(value, min), max);
    }
}
