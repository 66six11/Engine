using Asharia.Editor.Panels;
using Avalonia;
using Editor.Core.Models.Panels;

namespace Editor.Shell.Docking.DropTargets;

public sealed record EditorDockDropTarget(
    EditorDockDropOperation Operation,
    EditorDockDropGuideKind GuideKind,
    EditorDockArea? TargetArea,
    string? TargetId,
    Rect PreviewBounds,
    string Label,
    int? TargetIndex = null,
    double? SplitterFirstExtent = null,
    double? SplitterSecondExtent = null)
{
    public bool IsAccepted => Operation is not EditorDockDropOperation.Reject;

    public static EditorDockDropTarget Reject(Rect previewBounds, string label)
    {
        return new EditorDockDropTarget(
            EditorDockDropOperation.Reject,
            EditorDockDropGuideKind.Reject,
            null,
            null,
            previewBounds,
            label);
    }
}
