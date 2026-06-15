using System.Collections.Generic;
using Avalonia;
using Avalonia.Layout;

namespace Editor.Shell.Docking;

public sealed class EditorDockHitTestService
{
    private const double SplitterHitSlop = 8.0;
    private const double SplitterPreviewBreadth = 76.0;
    private const double PaneGuideCenterWidth = 132.0;
    private const double PaneGuideCenterHeight = 58.0;
    private const double PaneGuideHotspotWidth = 72.0;
    private const double PaneGuideHotspotHeight = 44.0;
    private const double PaneGuideInset = 12.0;
    private const double PaneInsertPreviewBreadth = 112.0;
    private const double RejectPreviewWidth = 172.0;
    private const double RejectPreviewHeight = 48.0;
    private const double FloatPreviewWidth = 260.0;
    private const double FloatPreviewHeight = 180.0;

    public EditorDockDropTarget HitTest(
        Point pointer,
        Rect workspaceBounds,
        IReadOnlyList<EditorDockPaneBounds> panes,
        IReadOnlyList<EditorDockSplitterBounds> splitters)
    {
        if (panes.Count == 0)
        {
            return CreateRejectTarget(pointer, "No dock target");
        }

        if (!workspaceBounds.Contains(pointer))
        {
            return CreateFloatTarget(pointer);
        }

        foreach (var splitter in splitters)
        {
            if (Inflate(splitter.Bounds, SplitterHitSlop, SplitterHitSlop).Contains(pointer))
            {
                return CreateSplitterTarget(splitter);
            }
        }

        foreach (var pane in panes)
        {
            if (pane.Bounds.Contains(pointer))
            {
                return HitTestPaneGuide(pointer, pane);
            }
        }

        return CreateRejectTarget(pointer, "Unavailable target");
    }

    private static EditorDockDropTarget HitTestPaneGuide(Point pointer, EditorDockPaneBounds pane)
    {
        var center = GetCenteredRect(pane.Bounds.Center, PaneGuideCenterWidth, PaneGuideCenterHeight);
        var top = GetCenteredRect(
            new Point(
                pane.Bounds.Center.X,
                pane.Bounds.Y + PaneGuideInset + (PaneGuideHotspotHeight / 2)),
            PaneGuideHotspotWidth,
            PaneGuideHotspotHeight);
        var right = GetCenteredRect(
            new Point(
                pane.Bounds.Right - PaneGuideInset - (PaneGuideHotspotWidth / 2),
                pane.Bounds.Center.Y),
            PaneGuideHotspotWidth,
            PaneGuideHotspotHeight);
        var bottom = GetCenteredRect(
            new Point(
                pane.Bounds.Center.X,
                pane.Bounds.Bottom - PaneGuideInset - (PaneGuideHotspotHeight / 2)),
            PaneGuideHotspotWidth,
            PaneGuideHotspotHeight);
        var left = GetCenteredRect(
            new Point(
                pane.Bounds.X + PaneGuideInset + (PaneGuideHotspotWidth / 2),
                pane.Bounds.Center.Y),
            PaneGuideHotspotWidth,
            PaneGuideHotspotHeight);

        if (center.Contains(pointer))
        {
            return CreateTabTarget(pane);
        }

        if (top.Contains(pointer))
        {
            return CreatePaneInsertTarget(EditorDockDropOperation.InsertTop, pane);
        }

        if (right.Contains(pointer))
        {
            return CreatePaneInsertTarget(EditorDockDropOperation.InsertRight, pane);
        }

        if (bottom.Contains(pointer))
        {
            return CreatePaneInsertTarget(EditorDockDropOperation.InsertBottom, pane);
        }

        if (left.Contains(pointer))
        {
            return CreatePaneInsertTarget(EditorDockDropOperation.InsertLeft, pane);
        }

        return CreateTabTarget(pane);
    }

    private static EditorDockDropTarget CreateTabTarget(EditorDockPaneBounds pane)
    {
        return new EditorDockDropTarget(
            EditorDockDropOperation.TabInto,
            EditorDockDropGuideKind.Merge,
            pane.Area,
            pane.PaneId,
            pane.Bounds,
            $"Tab into {pane.PaneId}");
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

    private static EditorDockDropTarget CreatePaneInsertTarget(
        EditorDockDropOperation operation,
        EditorDockPaneBounds pane)
    {
        return new EditorDockDropTarget(
            operation,
            EditorDockDropGuideKind.Insert,
            pane.Area,
            pane.PaneId,
            GetPaneInsertPreviewBounds(operation, pane.Bounds),
            GetPaneInsertLabel(operation, pane.PaneId));
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
            "Float panel");
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

    private static Rect GetPaneInsertPreviewBounds(EditorDockDropOperation operation, Rect paneBounds)
    {
        return operation switch
        {
            EditorDockDropOperation.InsertLeft => new Rect(
                paneBounds.X,
                paneBounds.Y,
                System.Math.Min(PaneInsertPreviewBreadth, paneBounds.Width),
                paneBounds.Height),
            EditorDockDropOperation.InsertRight => new Rect(
                paneBounds.Right - System.Math.Min(PaneInsertPreviewBreadth, paneBounds.Width),
                paneBounds.Y,
                System.Math.Min(PaneInsertPreviewBreadth, paneBounds.Width),
                paneBounds.Height),
            EditorDockDropOperation.InsertTop => new Rect(
                paneBounds.X,
                paneBounds.Y,
                paneBounds.Width,
                System.Math.Min(PaneInsertPreviewBreadth, paneBounds.Height)),
            EditorDockDropOperation.InsertBottom => new Rect(
                paneBounds.X,
                paneBounds.Bottom - System.Math.Min(PaneInsertPreviewBreadth, paneBounds.Height),
                paneBounds.Width,
                System.Math.Min(PaneInsertPreviewBreadth, paneBounds.Height)),
            _ => paneBounds,
        };
    }

    private static string GetPaneInsertLabel(EditorDockDropOperation operation, string paneId)
    {
        return operation switch
        {
            EditorDockDropOperation.InsertLeft => $"Insert left of {paneId}",
            EditorDockDropOperation.InsertRight => $"Insert right of {paneId}",
            EditorDockDropOperation.InsertTop => $"Insert above {paneId}",
            EditorDockDropOperation.InsertBottom => $"Insert below {paneId}",
            _ => $"Insert near {paneId}",
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
}
