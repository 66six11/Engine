using Avalonia;
using Avalonia.Layout;

namespace Editor.Shell.Docking.DropTargets;

public readonly record struct EditorDockSplitterBounds(
    string SplitterId,
    Orientation Orientation,
    Rect Bounds,
    Rect FirstBounds,
    Rect SecondBounds);
